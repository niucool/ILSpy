// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the minimalCorlibTypeProvider port (cpp/Decompiler/Metadata/
// MetadataExtensions.{hpp,cpp} -- the C# MetadataExtensions.cs lines 215-228):
// the static TypeProvider over a SimpleCompilation(MinimalCorlib.Instance) that
// the NullableContext / NullablePublicOnly / DefaultMember attribute-value
// decoders drive through CustomAttribute.DecodeValue. Every expectation is
// gold-pinned against the REAL ICSharpCode.Decompiler 11.0
// MetadataExtensions.MinimalAttributeTypeProvider / MinimalSignatureTypeProvider
// (the C:/temp-probe/MctpProbe gold reference; gold.txt line numbers cited per
// section). The gold pins:
//   * both accessors return the same provider instance (sameInstance=True);
//   * GetPrimitiveType over every ToKnownTypeCode-mappable code resolves the
//     MinimalCorlib definition (shape + the KnownTypeCache identity across
//     calls), and the unmappable codes resolve the SpecialType UnknownType
//     null object (FindType(None));
//   * GetSystemType / IsSystemType over System.Type;
//   * GetTypeFromSerializedName hits (definitions), composite forms (array /
//     pointer / byref), generic instantiations (the STRUCTURE pins -- the
//     composed ParameterizedType ReflectionName render is the known minimal-port
//     divergence, TypeProvider.hpp's file note), the UnknownType miss fallback
//     (fresh instance per call, gold sameInstance=False), and the
//     BadImageFormatException message for an invalid name;
//   * GetUnderlyingEnumType's non-enum passthrough quirk (a primitive handed
//     to the API answers its own code), the 0 code for non-primitive known
//     types, and the EnumUnderlyingTypeResolveException arms;
//   * the composite signature arms and the generic-instantiation arity rules.
// The reader-dependent arms (GetTypeFromDefinition / GetTypeFromReference /
// GetTypeFromSpecification) stay the TypeProvider.hpp convention-(b) divergence
// for the compilation-only ctor: the port throws std::logic_error where the C#
// reads through the caller's MetadataReader (pinned as the port contract).

#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TM = ILSpy::Decompiler::Metadata;
namespace TS = ILSpy::Decompiler::TypeSystem;

// The gold's IsRef rendering: "null" / "true" / "false" (the C# bool?).
std::string IsRefStr(const TS::IType& t) {
    auto r = t.IsReferenceType();
    if (!r) return "null";
    return *r ? "true" : "false";
}

// The gold's ktc rendering: the resolved definition's KnownTypeCode, -1 when
// GetDefinition() is null.
int KtcOf(const TS::IType& t) {
    const TS::ITypeDefinition* def = t.GetDefinition();
    return def ? static_cast<int>(def->KnownTypeCode()) : -1;
}

} // namespace

// ---------------------------------------------------------------------------
// The singleton contract (gold line 1: sameInstance=True). Both accessors are
// the C# properties over one static field; the port's two functions must
// return the same provider, and repeated calls must be stable.
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, BothAccessorsReturnTheSameProvider)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();
    TS::TypeProvider& sig = TM::MinimalSignatureTypeProvider();
    EXPECT_EQ(&attr, &sig);
    EXPECT_EQ(&attr, &TM::MinimalAttributeTypeProvider());
    // The provider's compilation is the minimal corlib (the gold drives every
    // member through the interface; the compilation surface is the same one).
    EXPECT_EQ(attr.Compilation().MainModule().AssemblyName(), "corlib");
}

// ---------------------------------------------------------------------------
// The primitive matrix (gold lines 4-38): GetPrimitiveType resolves the
// MinimalCorlib definition for every ToKnownTypeCode-mappable code. The gold's
// ktc numbers (the real KnownTypeCode values, String=18 / Void=19 /
// TypedReference=49 / IntPtr=28 ...) are the enum-member pins the port's
// KnownTypeCode carries after the value-hole fix.
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, PrimitiveMatrixMatchesGold)
{
    TS::TypeProvider& provider = TM::MinimalSignatureTypeProvider();

    struct Row { TM::PrimitiveTypeCode code; TS::TypeKind kind;
        const char* name; const char* reflection; const char* isRef;
        TS::KnownTypeCode ktc; };
    const Row rows[] = {
        { TM::PrimitiveTypeCode::Void, TS::TypeKind::Void, "Void",
          "System.Void", "null", TS::KnownTypeCode::Void },
        { TM::PrimitiveTypeCode::Boolean, TS::TypeKind::Struct, "Boolean",
          "System.Boolean", "false", TS::KnownTypeCode::Boolean },
        { TM::PrimitiveTypeCode::Char, TS::TypeKind::Struct, "Char",
          "System.Char", "false", TS::KnownTypeCode::Char },
        { TM::PrimitiveTypeCode::SByte, TS::TypeKind::Struct, "SByte",
          "System.SByte", "false", TS::KnownTypeCode::SByte },
        { TM::PrimitiveTypeCode::Byte, TS::TypeKind::Struct, "Byte",
          "System.Byte", "false", TS::KnownTypeCode::Byte },
        { TM::PrimitiveTypeCode::Int16, TS::TypeKind::Struct, "Int16",
          "System.Int16", "false", TS::KnownTypeCode::Int16 },
        { TM::PrimitiveTypeCode::UInt16, TS::TypeKind::Struct, "UInt16",
          "System.UInt16", "false", TS::KnownTypeCode::UInt16 },
        { TM::PrimitiveTypeCode::Int32, TS::TypeKind::Struct, "Int32",
          "System.Int32", "false", TS::KnownTypeCode::Int32 },
        { TM::PrimitiveTypeCode::UInt32, TS::TypeKind::Struct, "UInt32",
          "System.UInt32", "false", TS::KnownTypeCode::UInt32 },
        { TM::PrimitiveTypeCode::Int64, TS::TypeKind::Struct, "Int64",
          "System.Int64", "false", TS::KnownTypeCode::Int64 },
        { TM::PrimitiveTypeCode::UInt64, TS::TypeKind::Struct, "UInt64",
          "System.UInt64", "false", TS::KnownTypeCode::UInt64 },
        { TM::PrimitiveTypeCode::Single, TS::TypeKind::Struct, "Single",
          "System.Single", "false", TS::KnownTypeCode::Single },
        { TM::PrimitiveTypeCode::Double, TS::TypeKind::Struct, "Double",
          "System.Double", "false", TS::KnownTypeCode::Double },
        { TM::PrimitiveTypeCode::String, TS::TypeKind::Class, "String",
          "System.String", "true", TS::KnownTypeCode::String },
        { TM::PrimitiveTypeCode::TypedReference, TS::TypeKind::Struct,
          "TypedReference", "System.TypedReference", "false",
          TS::KnownTypeCode::TypedReference },
        { TM::PrimitiveTypeCode::IntPtr, TS::TypeKind::Struct, "IntPtr",
          "System.IntPtr", "false", TS::KnownTypeCode::IntPtr },
        { TM::PrimitiveTypeCode::UIntPtr, TS::TypeKind::Struct, "UIntPtr",
          "System.UIntPtr", "false", TS::KnownTypeCode::UIntPtr },
        { TM::PrimitiveTypeCode::Object, TS::TypeKind::Class, "Object",
          "System.Object", "true", TS::KnownTypeCode::Object },
    };
    for (const auto& r : rows) {
        TS::ITypePtr t = provider.GetPrimitiveType(r.code);
        ASSERT_NE(t, nullptr) << r.name;
        EXPECT_EQ(t->Kind(), r.kind) << r.name;
        EXPECT_EQ(t->Name(), std::string(r.name)) << r.name;
        EXPECT_EQ(t->ReflectionName(), std::string(r.reflection)) << r.name;
        EXPECT_EQ(t->TypeParameterCount(), 0) << r.name;
        EXPECT_EQ(IsRefStr(*t), std::string(r.isRef)) << r.name;
        EXPECT_EQ(KtcOf(*t), static_cast<int>(r.ktc)) << r.name;
        // The KnownTypeCache identity (gold prim:*:sameInstance=True): two
        // calls resolve the same definition instance.
        TS::ITypePtr t2 = provider.GetPrimitiveType(r.code);
        EXPECT_EQ(t.get(), t2.get()) << r.name;
    }
}

// ---------------------------------------------------------------------------
// The unmappable codes (gold lines 39-41: prim:Raw0 / prim:FnPtr /
// prim:SzArray): ToKnownTypeCode maps them to None, and FindType(None) is the
// SpecialType UnknownType null object (type=SpecialType, rn=?, kind=Unknown,
// ktc=-1, isRef=null).
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, UnmappablePrimitiveCodesResolveUnknownNullObject)
{
    TS::TypeProvider& provider = TM::MinimalSignatureTypeProvider();
    for (std::uint8_t raw : {0x00, 0x1B, 0x1D}) {
        TS::ITypePtr t = provider.GetPrimitiveType(
            static_cast<TM::PrimitiveTypeCode>(raw));
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Unknown) << raw;
        EXPECT_EQ(t->ReflectionName(), "?") << raw;
        EXPECT_EQ(t->Name(), "?") << raw;
        EXPECT_EQ(KtcOf(*t), -1) << raw;
        EXPECT_EQ(IsRefStr(*t), "null") << raw;
    }
}

// ---------------------------------------------------------------------------
// GetSystemType / IsSystemType (gold lines 42-48): System.Type resolves through
// the minimal corlib (CorlibTypeDefinition, Class, isRef=true, ktc=Type);
// IsSystemType answers true only for it (the same-instance call included).
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, GetSystemTypeAndIsSystemType)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();
    TS::TypeProvider& sig = TM::MinimalSignatureTypeProvider();

    TS::ITypePtr sysType = attr.GetSystemType();
    ASSERT_NE(sysType, nullptr);
    EXPECT_EQ(sysType->Kind(), TS::TypeKind::Class);
    EXPECT_EQ(sysType->Name(), "Type");
    EXPECT_EQ(sysType->ReflectionName(), "System.Type");
    EXPECT_EQ(sysType->TypeParameterCount(), 0);
    EXPECT_EQ(IsRefStr(*sysType), "true");
    EXPECT_EQ(KtcOf(*sysType), static_cast<int>(TS::KnownTypeCode::Type));

    // The KnownTypeCache identity (gold sysType:sameInstance=True).
    TS::ITypePtr sysType2 = attr.GetSystemType();
    EXPECT_EQ(sysType.get(), sysType2.get());

    // IsSystemType (gold isSystemType.self/string/int32/szarr).
    EXPECT_TRUE(attr.IsSystemType(*sysType));
    TS::ITypePtr str = sig.GetPrimitiveType(TM::PrimitiveTypeCode::String);
    TS::ITypePtr int32 = sig.GetPrimitiveType(TM::PrimitiveTypeCode::Int32);
    EXPECT_FALSE(attr.IsSystemType(*str));
    EXPECT_FALSE(attr.IsSystemType(*int32));
    EXPECT_FALSE(attr.IsSystemType(*sig.GetSZArrayType(str)));
}

// ---------------------------------------------------------------------------
// GetTypeFromSerializedName hits (gold lines 51-53): the plain-name form
// resolves the MinimalCorlib definition through ParseReflectionName's module
// walk; the composed forms (array / pointer / byref) build the composites.
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, GetTypeFromSerializedNameResolvesDefinitions)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();

    // ser:[System.String]: the definition (CorlibTypeDefinition, ktc=18).
    TS::ITypePtr str = attr.GetTypeFromSerializedName("System.String");
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->Kind(), TS::TypeKind::Class);
    EXPECT_EQ(str->Name(), "String");
    EXPECT_EQ(str->ReflectionName(), "System.String");
    EXPECT_EQ(IsRefStr(*str), "true");
    EXPECT_EQ(KtcOf(*str), static_cast<int>(TS::KnownTypeCode::String));

    // ser:[System.Collections.Generic.IEnumerable`1]: the open generic
    // definition (Interface, tpc=1, ktc=32).
    TS::ITypePtr ienum = attr.GetTypeFromSerializedName(
        "System.Collections.Generic.IEnumerable`1");
    ASSERT_NE(ienum, nullptr);
    EXPECT_EQ(ienum->Kind(), TS::TypeKind::Interface);
    EXPECT_EQ(ienum->Name(), "IEnumerable");
    EXPECT_EQ(ienum->ReflectionName(), "System.Collections.Generic.IEnumerable`1");
    EXPECT_EQ(ienum->TypeParameterCount(), 1);
    EXPECT_EQ(IsRefStr(*ienum), "true");
    EXPECT_EQ(KtcOf(*ienum),
              static_cast<int>(TS::KnownTypeCode::IEnumerableOfT));
}

TEST(MinimalCorlibTypeProviderTest, GetTypeFromSerializedNameComposites)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();

    // ser:[System.String[]]: ArrayType, reflection "System.String[]",
    // isRef=true, no definition. (The Name pin is skipped: the port's
    // composing-type Name renders without the [] suffix -- the documented
    // minimal-port divergence the gold's name=<String[]> shows.)
    TS::ITypePtr arr = attr.GetTypeFromSerializedName("System.String[]");
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->Kind(), TS::TypeKind::Array);
    EXPECT_EQ(arr->ReflectionName(), "System.String[]");
    EXPECT_EQ(arr->TypeParameterCount(), 0);
    EXPECT_EQ(IsRefStr(*arr), "true");
    EXPECT_EQ(KtcOf(*arr), -1);

    // ser:[System.String*]: PointerType, isRef=null.
    TS::ITypePtr ptr = attr.GetTypeFromSerializedName("System.String*");
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->Kind(), TS::TypeKind::Pointer);
    EXPECT_EQ(ptr->ReflectionName(), "System.String*");
    EXPECT_EQ(IsRefStr(*ptr), "null");

    // ser:[System.String&]: ByReferenceType, isRef=null.
    TS::ITypePtr byref = attr.GetTypeFromSerializedName("System.String&");
    ASSERT_NE(byref, nullptr);
    EXPECT_EQ(byref->Kind(), TS::TypeKind::ByReference);
    EXPECT_EQ(byref->ReflectionName(), "System.String&");
    EXPECT_EQ(IsRefStr(*byref), "null");
}

// ---------------------------------------------------------------------------
// GetTypeFromSerializedName generic instantiations (gold lines 54-55, 59, 62):
// the STRUCTURE pins -- the composed ParameterizedType's own ReflectionName is
// the known minimal-port render divergence (TypeProvider.hpp's file note), so
// the pins are the Kind / tpc / the generic's and the args' ReflectionNames
// (the genericRn / args gold fields).
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, GetTypeFromSerializedNameGenericInstantiation)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();

    // ser:[System.Collections.Generic.IEnumerable`1[[System.Int32]]]:
    // ParameterizedType over the resolved definition, one arg.
    TS::ITypePtr inst = attr.GetTypeFromSerializedName(
        "System.Collections.Generic.IEnumerable`1[[System.Int32]]");
    ASSERT_NE(inst, nullptr);
    auto* pt = dynamic_cast<const TS::ParameterizedType*>(inst.get());
    ASSERT_NE(pt, nullptr);
    EXPECT_EQ(pt->Kind(), TS::TypeKind::Interface);
    EXPECT_EQ(pt->TypeParameterCount(), 1);
    EXPECT_EQ(pt->IsByRefLike(), false);
    EXPECT_EQ(IsRefStr(*pt), "true");
    EXPECT_EQ(KtcOf(*pt),
              static_cast<int>(TS::KnownTypeCode::IEnumerableOfT));
    ASSERT_NE(pt->GenericType(), nullptr);
    EXPECT_EQ(pt->GenericType()->ReflectionName(),
              "System.Collections.Generic.IEnumerable`1");
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    EXPECT_EQ(pt->TypeArguments()[0]->ReflectionName(), "System.Int32");

    // ser:[System.Nullable`1[[System.Int32]]]: a Struct-kind generic -- the
    // ParameterizedType DELEGATES its kind and reference-ness to the generic
    // (gold kind=Struct isRef=false ktc=44).
    TS::ITypePtr nullable = attr.GetTypeFromSerializedName(
        "System.Nullable`1[[System.Int32]]");
    ASSERT_NE(nullable, nullptr);
    auto* npt = dynamic_cast<const TS::ParameterizedType*>(nullable.get());
    ASSERT_NE(npt, nullptr);
    EXPECT_EQ(npt->Kind(), TS::TypeKind::Struct);
    EXPECT_EQ(IsRefStr(*nullable), "false");
    EXPECT_EQ(KtcOf(*nullable), static_cast<int>(TS::KnownTypeCode::NullableOfT));
    ASSERT_NE(npt->GenericType(), nullptr);
    EXPECT_EQ(npt->GenericType()->ReflectionName(), "System.Nullable`1");

    // ser:[System.Collections.Generic.IEnumerable`2[[System.Int32],[System.String]]]:
    // the arity-2 miss resolves an UnknownType generic; the ParameterizedType
    // over it delegates the Unknown kind (gold kind=Unknown tpc=2 isRef=null)
    // and carries both args.
    TS::ITypePtr mismatch = attr.GetTypeFromSerializedName(
        "System.Collections.Generic.IEnumerable`2[[System.Int32],[System.String]]");
    ASSERT_NE(mismatch, nullptr);
    auto* mpt = dynamic_cast<const TS::ParameterizedType*>(mismatch.get());
    ASSERT_NE(mpt, nullptr);
    EXPECT_EQ(mpt->Kind(), TS::TypeKind::Unknown);
    EXPECT_EQ(mpt->TypeParameterCount(), 2);
    EXPECT_EQ(IsRefStr(*mismatch), "null");
    EXPECT_EQ(KtcOf(*mismatch), -1);
    ASSERT_NE(mpt->GenericType(), nullptr);
    EXPECT_EQ(mpt->GenericType()->ReflectionName(),
              "System.Collections.Generic.IEnumerable`2");
    ASSERT_EQ(mpt->TypeArguments().size(), 2u);
    EXPECT_EQ(mpt->TypeArguments()[0]->ReflectionName(), "System.Int32");
    EXPECT_EQ(mpt->TypeArguments()[1]->ReflectionName(), "System.String");
}

// ---------------------------------------------------------------------------
// GetTypeFromSerializedName misses (gold lines 56-57, 63): an unresolvable
// simple name is the UnknownType fallback (fresh instance per call -- gold
// ser:miss:sameInstance=False); a name without a namespace parses as a plain
// type name (gold ser:[bogus(] resolves UnknownType rn=<bogus(>, NOT an
// exception).
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, GetTypeFromSerializedNameUnknownFallbacks)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();

    TS::ITypePtr miss = attr.GetTypeFromSerializedName("System.Uri");
    ASSERT_NE(miss, nullptr);
    EXPECT_EQ(miss->Kind(), TS::TypeKind::Unknown);
    EXPECT_EQ(miss->ReflectionName(), "System.Uri");
    EXPECT_EQ(miss->Name(), "Uri");
    EXPECT_EQ(miss->TypeParameterCount(), 0);
    EXPECT_EQ(IsRefStr(*miss), "null");
    EXPECT_EQ(KtcOf(*miss), -1);
    // A fresh UnknownType per call (the C# news it up on every miss).
    TS::ITypePtr miss2 = attr.GetTypeFromSerializedName("System.Uri");
    EXPECT_NE(miss.get(), miss2.get());

    // ser:[System.Bogus]: the same fallback with a different name.
    TS::ITypePtr bogus = attr.GetTypeFromSerializedName("System.Bogus");
    ASSERT_NE(bogus, nullptr);
    EXPECT_EQ(bogus->ReflectionName(), "System.Bogus");
    EXPECT_EQ(bogus->Kind(), TS::TypeKind::Unknown);

    // ser:[bogus(]: no namespace dot -- the whole input is the type name.
    TS::ITypePtr paren = attr.GetTypeFromSerializedName("bogus(");
    ASSERT_NE(paren, nullptr);
    EXPECT_EQ(paren->Kind(), TS::TypeKind::Unknown);
    EXPECT_EQ(paren->ReflectionName(), "bogus(");
    EXPECT_EQ(paren->Name(), "bogus(");
}

// ---------------------------------------------------------------------------
// GetTypeFromSerializedName invalid names (gold line 61): the empty string
// throws the BadImageFormatException wrapping the ReflectionNameParseException
// -- the port's std::invalid_argument with the exact formatted message (the
// trailing space included: the inner message is "Invalid type name: " + the
// empty input).
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, GetTypeFromSerializedNameInvalidNameThrows)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();
    try {
        attr.GetTypeFromSerializedName("");
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
                     "Invalid type name: \"\": Invalid type name: ");
    }
}

// ---------------------------------------------------------------------------
// GetUnderlyingEnumType (gold lines 64-73): the non-enum passthrough quirk --
// GetEnumUnderlyingType returns a non-enum type unmodified, so a primitive
// handed to the API answers its OWN code (int32->Int32, string->String,
// void->Void, object->Object); a non-primitive known type answers 0 (the C#
// default); an array (GetDefinition()==null) and the UnknownType fallback
// throw EnumUnderlyingTypeResolveException.
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, GetUnderlyingEnumTypeMatrix)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();
    TS::TypeProvider& sig = TM::MinimalSignatureTypeProvider();

    TS::ITypePtr int32 = sig.GetPrimitiveType(TM::PrimitiveTypeCode::Int32);
    TS::ITypePtr str = sig.GetPrimitiveType(TM::PrimitiveTypeCode::String);

    // enumUnder:int32 -> 8 (Int32); the passthrough quirk.
    EXPECT_EQ(static_cast<int>(attr.GetUnderlyingEnumType(*int32)),
              static_cast<int>(TM::PrimitiveTypeCode::Int32));
    // enumUnder:string -> 14 (String).
    EXPECT_EQ(static_cast<int>(attr.GetUnderlyingEnumType(*str)),
              static_cast<int>(TM::PrimitiveTypeCode::String));
    // enumUnder:void -> 1 (Void).
    EXPECT_EQ(static_cast<int>(attr.GetUnderlyingEnumType(
                  *sig.GetPrimitiveType(TM::PrimitiveTypeCode::Void))),
              static_cast<int>(TM::PrimitiveTypeCode::Void));
    // enumUnder:object -> 28 (0x1C, Object).
    EXPECT_EQ(static_cast<int>(attr.GetUnderlyingEnumType(
                  *sig.GetPrimitiveType(TM::PrimitiveTypeCode::Object))),
              static_cast<int>(TM::PrimitiveTypeCode::Object));
    // enumUnder:sysType -> 0 (System.Type is not a primitive known type).
    EXPECT_EQ(static_cast<int>(attr.GetUnderlyingEnumType(
                  *attr.GetSystemType())), 0);
    // enumUnder:nullableDef -> 0.
    EXPECT_EQ(static_cast<int>(attr.GetUnderlyingEnumType(
                  *attr.GetTypeFromSerializedName("System.Nullable`1"))), 0);
    // enumUnder:genInst -> 0 (the ParameterizedType delegates GetDefinition
    // to the generic's definition, a non-primitive known type).
    TS::ITypePtr inst = sig.GetGenericInstantiation(
        attr.GetTypeFromSerializedName("System.Collections.Generic.IEnumerable`1"),
        {int32});
    EXPECT_EQ(static_cast<int>(attr.GetUnderlyingEnumType(*inst)), 0);

    // enumUnder:szarr -> EnumUnderlyingTypeResolveException (an ArrayType
    // has no definition).
    try {
        attr.GetUnderlyingEnumType(*sig.GetSZArrayType(str));
        FAIL() << "expected EnumUnderlyingTypeResolveException";
    } catch (const TM::EnumUnderlyingTypeResolveException&) {
    }
    // enumUnder:unknown -> EnumUnderlyingTypeResolveException (the UnknownType
    // fallback has no definition).
    try {
        attr.GetUnderlyingEnumType(
            *attr.GetTypeFromSerializedName("System.Uri"));
        FAIL() << "expected EnumUnderlyingTypeResolveException";
    } catch (const TM::EnumUnderlyingTypeResolveException&) {
    }
}

// ---------------------------------------------------------------------------
// The composite signature arms (gold lines 74-85): SZArray / rank-2 array /
// pointer / byref over a resolved primitive, and GetGenericInstantiation's
// arity rules (the mismatch arms return the generic as-is -- the C# comment:
// the generic can be from another assembly that does not have the typical `N
// suffix and is not loaded).
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, CompositeSignatureArmsMatchGold)
{
    TS::TypeProvider& attr = TM::MinimalAttributeTypeProvider();
    TS::TypeProvider& sig = TM::MinimalSignatureTypeProvider();
    TS::ITypePtr str = sig.GetPrimitiveType(TM::PrimitiveTypeCode::String);
    TS::ITypePtr int32 = sig.GetPrimitiveType(TM::PrimitiveTypeCode::Int32);

    // szarr: ArrayType "System.String[]", isRef=true.
    {
        TS::ITypePtr t = sig.GetSZArrayType(str);
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Array);
        EXPECT_EQ(t->ReflectionName(), "System.String[]");
        EXPECT_EQ(IsRefStr(*t), "true");
    }
    // arr2: rank 2 -- "System.String[,]".
    {
        TM::ArrayShape shape;
        shape.Rank = 2;
        TS::ITypePtr t = sig.GetArrayType(str, shape);
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->ReflectionName(), "System.String[,]");
        EXPECT_EQ(IsRefStr(*t), "true");
    }
    // ptr / byref.
    EXPECT_EQ(sig.GetPointerType(str)->ReflectionName(), "System.String*");
    EXPECT_EQ(sig.GetByReferenceType(str)->ReflectionName(), "System.String&");

    // genInst: the arity match wraps the generic in a ParameterizedType.
    TS::ITypePtr ienum = attr.GetTypeFromSerializedName(
        "System.Collections.Generic.IEnumerable`1");
    {
        TS::ITypePtr t = sig.GetGenericInstantiation(ienum, {int32});
        auto* pt = dynamic_cast<const TS::ParameterizedType*>(t.get());
        ASSERT_NE(pt, nullptr);
        EXPECT_EQ(pt->GenericType()->ReflectionName(),
                  "System.Collections.Generic.IEnumerable`1");
        ASSERT_EQ(pt->TypeArguments().size(), 1u);
        EXPECT_EQ(pt->TypeArguments()[0]->ReflectionName(), "System.Int32");
    }
    // genInstMismatch / genInstMismatch2: wrong arity -- the generic as-is.
    {
        TS::ITypePtr t = sig.GetGenericInstantiation(ienum, {});
        EXPECT_EQ(t.get(), ienum.get());
        TS::ITypePtr t2 = sig.GetGenericInstantiation(ienum, {int32, str});
        EXPECT_EQ(t2.get(), ienum.get());
    }
    // genInstNonGeneric: a non-generic type ignores the arguments.
    {
        TS::ITypePtr strDef = attr.GetTypeFromSerializedName("System.String");
        TS::ITypePtr t = sig.GetGenericInstantiation(strDef, {int32});
        EXPECT_EQ(t.get(), strDef.get());
    }
}

// ---------------------------------------------------------------------------
// The reader-dependent arms (the port's documented divergence,
// TypeProvider.hpp convention (b)): the compilation-only provider cannot read
// metadata rows, so GetTypeFromDefinition / GetTypeFromReference /
// GetTypeFromSpecification throw std::logic_error naming the limitation --
// where the C# reads through the caller's MetadataReader and constructs the
// UnknownType fallback / resolves through the compilation's modules.
// ---------------------------------------------------------------------------
TEST(MinimalCorlibTypeProviderTest, ReaderDependentArmsThrowTheDocumentedDivergence)
{
    TS::TypeProvider& sig = TM::MinimalSignatureTypeProvider();
    TS::GenericContext context({}, {});

    EXPECT_THROW(sig.GetTypeFromDefinition(0x02000001, 0x12),
                 std::logic_error);
    EXPECT_THROW(sig.GetTypeFromReference(0x01000001, 0x12),
                 std::logic_error);
    EXPECT_THROW(sig.GetTypeFromSpecification(0x1B000001, 0x12, context),
                 std::logic_error);
}
