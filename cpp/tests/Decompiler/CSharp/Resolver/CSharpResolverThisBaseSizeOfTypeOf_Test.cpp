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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver sizeof / this / base / typeof regions
// (cpp/Decompiler/CSharp/Resolver/CSharpResolver.{hpp,cpp}, the port of
// CSharpResolver.cs lines 2591-2626 + 2628-2667 + 2935-2937).
//
// The load-bearing cruxes:
//  (a) SIZEOF: the result type is the REGISTERED Int32 (the type-cache identity)
//      and the constant size comes from the primitive TypeCode table
//      (bool/sbyte/byte -> 1 ... long/ulong/double -> 8); an ENUM reads its size
//      through its UNDERLYING type; everything outside the table (decimal,
//      DateTime, a reference type) has NO constant size, and a `sizeof` of a
//      reference type is an ERROR (the SizeOfResolveResult IsError override);
//  (b) THIS: no current type definition is the UnknownError SINGLETON (pointer
//      identity); a non-generic current type definition is the definition itself;
//      a GENERIC current type definition SELF-PARAMETERIZES -- the result type is
//      a ParameterizedType over the definition whose type arguments are the
//      DECLARED type parameters (pointer identity on both);
//  (c) BASE: no current type definition (or an all-interface/unknown base list) is
//      the UnknownError singleton; the FIRST non-Interface non-Unknown direct base
//      wins and the ThisResolveResult marks CausesNonVirtualInvocation (the `base`
//      member invocations are non-virtual);
//  (d) TYPEOF: the result type is the REGISTERED System.Type and the referenced
//      type is carried by pointer identity.

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/SizeOfResolveResult.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::SizeOfResolveResult;
using ILSpy::Decompiler::Semantics::ThisResolveResult;
using ILSpy::Decompiler::Semantics::TypeOfResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

// A LookupTypeDefinition whose IsReferenceType is DEFINITE per its kind (the real
// metadata model: a struct/enum is a value type, everything else a reference type --
// the SizeOfResolveResult IsError override reads it; the CSharpResolverBinaryOperator
// KindDef precedent).
class KindDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override {
        switch (Kind()) {
            case TypeKind::Struct:
            case TypeKind::Enum:
                return false;
            default:
                return true;
        }
    }
};

// A fresh shared compilation with every KnownTypeCode the region resolves through
// `FindType` registered as a shared-managed KindDef (Int32 for the sizeof result
// type, Type for the typeof result type -- an unregistered code would fall back to
// the compilation's non-shared unknown stub, whose `shared_from_this` cannot recover
// a handle), plus one REGISTERED instance per code the identity assertions target
// (the type-cache model: the FindType result IS the accessor instance).
struct Fixture {
    std::unique_ptr<LookupCompilation> compilation =
        std::make_unique<LookupCompilation>();
    std::vector<std::shared_ptr<KindDef>> defs;

    // The real .NET kind per code: Object/DBNull/String (and System.Type) are
    // classes, the primitives and DateTime are structs.
    static TypeKind KindForCode(KnownTypeCode code) {
        switch (code) {
            case KnownTypeCode::Object:
            case KnownTypeCode::DBNull:
            case KnownTypeCode::String:
            case KnownTypeCode::Type:
                return TypeKind::Class;
            default:
                return TypeKind::Struct;
        }
    }

    void RegisterCode(KnownTypeCode code) {
        std::string name = "T" + std::to_string(static_cast<int>(code));
        auto def = std::make_shared<KindDef>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)),
            KindForCode(code), Accessibility::Public, *compilation, nullptr, code);
        compilation->RegisterKnownType(code, def.get());
        defs.push_back(std::move(def));
    }

    Fixture() {
        for (int raw = static_cast<int>(KnownTypeCode::Object);
             raw <= static_cast<int>(KnownTypeCode::String); ++raw) {
            RegisterCode(static_cast<KnownTypeCode>(raw));
        }
        // The typeof result type (KnownTypeCode::Type sits past String in the enum
        // order, so the loop above does not cover it).
        RegisterCode(KnownTypeCode::Type);
    }
};

Fixture& Fix() {
    static Fixture fixture;
    return fixture;
}

LookupCompilation& Compilation() { return *Fix().compilation; }

// The REGISTERED definition for a known type code (the FindType identity target).
std::shared_ptr<KindDef> Def(KnownTypeCode code) {
    for (const auto& t : Fix().defs) {
        if (t->KnownTypeCode() == code)
            return t;
    }
    return nullptr;
}

std::shared_ptr<KindDef> Int32Def() { return Def(KnownTypeCode::Int32); }
std::shared_ptr<KindDef> StringDef() { return Def(KnownTypeCode::String); }

// A shared-managed non-generic class definition with the given name and kind.
std::shared_ptr<KindDef> MakeDef(const std::string& name, TypeKind kind,
                                  int typeParameterCount = 0) {
    return std::make_shared<KindDef>(
        name, "", FullTypeName(TopLevelTypeName("", name, typeParameterCount)),
        kind, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// A shared-managed enum definition with the given registered underlying primitive.
std::shared_ptr<KindDef> MakeEnum(KnownTypeCode underlyingCode) {
    auto e = MakeDef("E_" + std::to_string(static_cast<int>(underlyingCode)),
                     TypeKind::Enum);
    e->SetEnumUnderlyingType(Def(underlyingCode));
    return e;
}

// A fresh shared-managed resolver (the make_shared discipline).
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// --- ResolveSizeOf ---------------------------------------------------------------------------

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, SizeOfInt32Folds4OverTheRegisteredInt32Type)
{
    auto resolver = MakeResolver();
    auto result = resolver->ResolveSizeOf(*Int32Def());
    auto sizeOf = dynamic_cast<const SizeOfResolveResult*>(result.get());
    ASSERT_NE(sizeOf, nullptr);
    // The expression's own type is the REGISTERED Int32 (identity, not a fresh type).
    EXPECT_EQ(&result->Type(), Int32Def().get());
    EXPECT_TRUE(result->IsCompileTimeConstant());
    EXPECT_EQ(std::any_cast<int>(result->ConstantValue()), 4);
    EXPECT_FALSE(result->IsError());  // Int32 is a struct
    EXPECT_EQ(&sizeOf->ReferencedType(), Int32Def().get());
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, SizeOfPrimitiveTypeCodeTableFoldsTheDocumentedSizes)
{
    auto resolver = MakeResolver();
    struct Entry { KnownTypeCode code; int expected; };
    const Entry table[] = {
        { KnownTypeCode::Boolean, 1 }, { KnownTypeCode::SByte, 1 },
        { KnownTypeCode::Byte, 1 },
        { KnownTypeCode::Char, 2 }, { KnownTypeCode::Int16, 2 },
        { KnownTypeCode::UInt16, 2 },
        { KnownTypeCode::UInt32, 4 }, { KnownTypeCode::Single, 4 },
        { KnownTypeCode::UInt64, 8 }, { KnownTypeCode::Double, 8 },
    };
    for (const Entry& entry : table) {
        auto result = resolver->ResolveSizeOf(*Def(entry.code));
        EXPECT_TRUE(result->IsCompileTimeConstant())
            << "code " << static_cast<int>(entry.code) << " should fold";
        ASSERT_TRUE(result->ConstantValue().has_value());
        EXPECT_EQ(std::any_cast<int>(result->ConstantValue()), entry.expected)
            << "code " << static_cast<int>(entry.code) << " folded the wrong size";
    }
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, SizeOfEnumFoldsThroughItsUnderlyingType)
{
    auto resolver = MakeResolver();
    // An enum whose underlying is Byte folds 1; whose underlying is Int64 folds 8.
    auto overByte = MakeEnum(KnownTypeCode::Byte);
    auto resultByte = resolver->ResolveSizeOf(*overByte);
    EXPECT_TRUE(resultByte->IsCompileTimeConstant());
    ASSERT_TRUE(resultByte->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(resultByte->ConstantValue()), 1);
    EXPECT_FALSE(resultByte->IsError());  // the enum kind is a value type

    auto overLong = MakeEnum(KnownTypeCode::Int64);
    auto resultLong = resolver->ResolveSizeOf(*overLong);
    EXPECT_TRUE(resultLong->IsCompileTimeConstant());
    ASSERT_TRUE(resultLong->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(resultLong->ConstantValue()), 8);
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, SizeOfEnumWithoutDefinitionHasNoConstantSize)
{
    // A Kind == Enum type whose definition does not resolve: the C# would NRE at
    // `GetDefinition().EnumUnderlyingType`; the port's documented safe fallback reads
    // the type's own TypeCode (Empty for a non-definition) -- no constant size, no
    // error.
    auto resolver = MakeResolver();
    auto enumLike = std::make_shared<SpecialType>(TypeKind::Enum, false);
    auto result = resolver->ResolveSizeOf(*enumLike);
    EXPECT_FALSE(result->IsCompileTimeConstant());
    EXPECT_FALSE(result->IsError());
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, SizeOfNonTableTypesHaveNoConstantSize)
{
    // Decimal and DateTime have TypeCodes that are NOT in the primitive-size table.
    auto resolver = MakeResolver();
    for (KnownTypeCode code : { KnownTypeCode::Decimal, KnownTypeCode::DateTime }) {
        auto result = resolver->ResolveSizeOf(*Def(code));
        EXPECT_FALSE(result->IsCompileTimeConstant())
            << "code " << static_cast<int>(code) << " is outside the size table";
    }
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, SizeOfReferenceTypeIsAnError)
{
    // A `sizeof` of a reference type is an error (the SizeOfResolveResult IsError
    // override: `referencedType.IsReferenceType != false`).
    auto resolver = MakeResolver();
    auto result = resolver->ResolveSizeOf(*StringDef());
    EXPECT_TRUE(result->IsError());
    EXPECT_FALSE(result->IsCompileTimeConstant());
}

// --- ResolveThisReference --------------------------------------------------------------------

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, ThisWithoutCurrentTypeDefinitionIsTheUnknownErrorSingleton)
{
    auto resolver = MakeResolver();
    auto result = resolver->ResolveThisReference();
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, ThisOfNonGenericCurrentTypeIsTheDefinitionItself)
{
    auto currentType = MakeDef("C", TypeKind::Class);
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(currentType.get());
    auto result = resolver->ResolveThisReference();
    auto thisResult = dynamic_cast<const ThisResolveResult*>(result.get());
    ASSERT_NE(thisResult, nullptr);
    EXPECT_EQ(&result->Type(), currentType.get());
    EXPECT_FALSE(thisResult->CausesNonVirtualInvocation());
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, ThisOfGenericCurrentTypeIsSelfParameterized)
{
    auto currentType = MakeDef("C", TypeKind::Class, /*typeParameterCount=*/2);
    auto tp0 = std::make_shared<LookupTypeParameter>("T0");
    auto tp1 = std::make_shared<LookupTypeParameter>("T1");
    currentType->SetTypeParameters({ tp0.get(), tp1.get() });
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(currentType.get());
    auto result = resolver->ResolveThisReference();
    auto thisResult = dynamic_cast<const ThisResolveResult*>(result.get());
    ASSERT_NE(thisResult, nullptr);
    EXPECT_FALSE(thisResult->CausesNonVirtualInvocation());
    // The `this` inside `C<T0,T1>` has type `C<T0,T1>` -- the definition
    // parameterized with its OWN DECLARED type parameters.
    auto parameterized = dynamic_cast<const ParameterizedType*>(&result->Type());
    ASSERT_NE(parameterized, nullptr);
    EXPECT_EQ(parameterized->GenericType().get(), currentType.get());
    ASSERT_EQ(parameterized->TypeArguments().size(), 2u);
    EXPECT_EQ(parameterized->TypeArguments()[0].get(), tp0.get());
    EXPECT_EQ(parameterized->TypeArguments()[1].get(), tp1.get());
}

// --- ResolveBaseReference --------------------------------------------------------------------

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, BaseWithoutCurrentTypeDefinitionIsTheUnknownErrorSingleton)
{
    auto resolver = MakeResolver();
    auto result = resolver->ResolveBaseReference();
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, BaseReturnsTheFirstNonInterfaceBaseMarkingNonVirtual)
{
    auto currentType = MakeDef("C", TypeKind::Class);
    auto iface = MakeDef("I", TypeKind::Interface);
    auto baseClass = MakeDef("B", TypeKind::Class);
    currentType->AddDirectBaseType(iface);
    currentType->AddDirectBaseType(baseClass);
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(currentType.get());
    auto result = resolver->ResolveBaseReference();
    auto thisResult = dynamic_cast<const ThisResolveResult*>(result.get());
    ASSERT_NE(thisResult, nullptr);
    EXPECT_EQ(&result->Type(), baseClass.get());
    EXPECT_TRUE(thisResult->CausesNonVirtualInvocation());
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, BaseSkipsUnknownBases)
{
    auto currentType = MakeDef("C", TypeKind::Class);
    auto unknown = MakeDef("U", TypeKind::Unknown);
    auto baseClass = MakeDef("B", TypeKind::Class);
    currentType->AddDirectBaseType(unknown);
    currentType->AddDirectBaseType(baseClass);
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(currentType.get());
    auto result = resolver->ResolveBaseReference();
    auto thisResult = dynamic_cast<const ThisResolveResult*>(result.get());
    ASSERT_NE(thisResult, nullptr);
    EXPECT_EQ(&result->Type(), baseClass.get());
}

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, BaseOfAnAllInterfaceBaseListIsTheUnknownErrorSingleton)
{
    auto currentType = MakeDef("C", TypeKind::Class);
    auto iface = MakeDef("I", TypeKind::Interface);
    currentType->AddDirectBaseType(iface);
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(currentType.get());
    auto result = resolver->ResolveBaseReference();
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

// --- ResolveTypeOf ---------------------------------------------------------------------------

TEST(CSharpResolverThisBaseSizeOfTypeOfTest, TypeOfCarriesTheRegisteredSystemTypeAndTheReferencedType)
{
    auto resolver = MakeResolver();
    auto referenced = MakeDef("C", TypeKind::Class);
    auto result = resolver->ResolveTypeOf(*referenced);
    auto typeOf = dynamic_cast<const TypeOfResolveResult*>(result.get());
    ASSERT_NE(typeOf, nullptr);
    // The expression's own type is the REGISTERED System.Type.
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Type).get());
    EXPECT_EQ(&typeOf->ReferencedType(), referenced.get());
}

} // namespace
