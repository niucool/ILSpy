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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the MetadataTypeDefinition + MetadataTypeParameter port
// (TypeSystem/Implementation/MetadataTypeDefinition.{hpp,cpp} +
// MetadataTypeParameter.{hpp,cpp}): the ctor's eagerly-computed identity
// surface over the real .NET Framework 4.8 mscorlib / .NET 10 CoreLib /
// System.Core, every expectation gold-dumped from the REAL
// ICSharpCode.Decompiler 11.0 driven over the identical fixtures
// (C:/temp-probe/MtdProbe/gold_final.txt: SimpleCompilations whose PEFile
// main modules resolve to the real MetadataModule). The lazy member family,
// the attribute snapshot, and the base-type resolution are the loud
// deferrals of this slice; their tests pin the deferral contracts alongside
// the real Void early-exit / NestedTypes-only / ExtensionInfo-null arms.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCache.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeParameter.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* CoreLibPath() {
    // The newest installed .NET 10 runtime's System.Private.CoreLib (the
    // only local fixture with ref structs / readonly structs / the
    // AllowByRefLike type-parameter bit).
    return "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\"
           "System.Private.CoreLib.dll";
}

const char* SystemCorePath() {
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.Core.dll";
}

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

// A compilation whose main module is settable and whose FindType routes
// through the ported KnownTypeCache over the module -- the C#
// SimpleCompilation.FindType shape (the real engine resolves the enum
// underlying type through the same cache, so the port's EnumUnderlyingType
// matches the gold ReflectionName "System.Int32").
class CacheCompilation : public TS::ICompilation {
public:
    CacheCompilation() = default;

    void SetMainModule(const TS::IModule* module) { mainModule_ = module; }

    // --- ICompilation ---
    const TS::IModule& MainModule() const override { return *mainModule_; }
    std::vector<const TS::IModule*> Modules() const override
    {
        return std::vector<const TS::IModule*>{ mainModule_ };
    }
    std::vector<const TS::IModule*> ReferencedModules() const override { return {}; }
    const TS::INamespace& RootNamespace() const override
    {
        return mainModule_->RootNamespace();
    }
    const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const TS::IType& FindType(TS::KnownTypeCode code) const override
    {
        return knownTypes_.FindType(code);
    }
    const TS::StringComparer& NameComparer() const override
    {
        return TS::StringComparer::Ordinal();
    }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    TS::TypeSystemOptions TypeSystemOptions() const override
    {
        return TS::TypeSystemOptions::Default;
    }

private:
    const TS::IModule* mainModule_ = nullptr;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
    TS::KnownTypeCache knownTypes_{ *this };
};

// The mscorlib fixture: the real file, the module over it, the compilation
// it becomes the main module of.
struct MscorlibFixture {
    TM::MetadataFile file{ MscorlibPath() };
    CacheCompilation compilation;
    TS::MetadataModule module{ compilation, &file, TS::TypeSystemOptions::Default };

    MscorlibFixture() { compilation.SetMainModule(&module); }

    const TS::ITypeDefinition* Type(const char* ns, const char* name, int arity = 0)
    {
        const TS::ITypeDefinition* definition = module.GetTypeDefinition(
            TS::TopLevelTypeName(ns, name, arity));
        // No silent returns in tests: a missing fixture type is a failure.
        EXPECT_NE(definition, nullptr)
            << "fixture type " << ns << "." << name << "`" << arity;
        return definition;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The kind chain + the KnownTypeCode loop (the gold kind/knownTypeCode/
// isReferenceType/accessibility/flag matrix)
// ---------------------------------------------------------------------------

TEST(MetadataTypeDefinitionTest, KindAndFlagMatrixMatchesGoldOverMscorlib)
{
    MscorlibFixture f;

    struct Expected {
        const char* ns;
        const char* name;
        int arity;
        TS::TypeKind kind;
        TS::KnownTypeCode knownTypeCode;
        TS::Accessibility accessibility;
        bool isStatic;
        bool isAbstract;
        bool isSealed;
        std::optional<bool> isReferenceType;
        std::uint32_t token;
    };
    const Expected expected[] = {
        // kind, ktc, accessibility, isStatic, isAbstract, isSealed,
        // isReferenceType, token -- every row from gold_final.txt.
        { "System", "Object", 0, TS::TypeKind::Class, TS::KnownTypeCode::Object,
          TS::Accessibility::Public, false, false, false,
          std::optional<bool>(true), 0x0200003Du },
        { "System", "String", 0, TS::TypeKind::Class, TS::KnownTypeCode::String,
          TS::Accessibility::Public, false, false, true,
          std::optional<bool>(true), 0x02000073u },
        { "System", "Int32", 0, TS::TypeKind::Struct, TS::KnownTypeCode::Int32,
          TS::Accessibility::Public, false, false, true,
          std::optional<bool>(false), 0x020000FBu },
        { "System", "Void", 0, TS::TypeKind::Void, TS::KnownTypeCode::Void,
          TS::Accessibility::Public, false, false, true,
          std::optional<bool>(false), 0x0200015Cu },
        { "System", "Enum", 0, TS::TypeKind::Class, TS::KnownTypeCode::Enum,
          TS::Accessibility::Public, false, true, false,
          std::optional<bool>(true), 0x020000DAu },
        { "System", "ValueType", 0, TS::TypeKind::Class,
          TS::KnownTypeCode::ValueType, TS::Accessibility::Public, false,
          true, false, std::optional<bool>(true), 0x02000159u },
        { "System", "MulticastDelegate", 0, TS::TypeKind::Class,
          TS::KnownTypeCode::MulticastDelegate, TS::Accessibility::Public,
          false, true, false, std::optional<bool>(true), 0x02000087u },
        { "System", "Delegate", 0, TS::TypeKind::Class,
          TS::KnownTypeCode::Delegate, TS::Accessibility::Public, false,
          true, false, std::optional<bool>(true), 0x02000085u },
        { "System", "Array", 0, TS::TypeKind::Class, TS::KnownTypeCode::Array,
          TS::Accessibility::Public, false, true, false,
          std::optional<bool>(true), 0x02000055u },
        { "System", "Attribute", 0, TS::TypeKind::Class,
          TS::KnownTypeCode::Attribute, TS::Accessibility::Public, false,
          true, false, std::optional<bool>(true), 0x020000ADu },
        { "System", "Math", 0, TS::TypeKind::Class, TS::KnownTypeCode::None,
          TS::Accessibility::Public, true, true, true,
          std::optional<bool>(true), 0x0200010Bu },
        { "System.Text", "StringBuilder", 0, TS::TypeKind::Class,
          TS::KnownTypeCode::None, TS::Accessibility::Public, false, false,
          true, std::optional<bool>(true), 0x02000A58u },
        { "System.Collections.Generic", "List", 1, TS::TypeKind::Class,
          TS::KnownTypeCode::None, TS::Accessibility::Public, false, false,
          false, std::optional<bool>(true), 0x020004DCu },
        { "System.Collections.Generic", "IEnumerable", 1,
          TS::TypeKind::Interface, TS::KnownTypeCode::IEnumerableOfT,
          TS::Accessibility::Public, false, true, false,
          std::optional<bool>(true), 0x020004D3u },
        { "System.Collections.Generic", "Dictionary", 2, TS::TypeKind::Class,
          TS::KnownTypeCode::None, TS::Accessibility::Public, false, false,
          false, std::optional<bool>(true), 0x020004BFu },
        { "System.Collections.Generic", "IComparer", 1, TS::TypeKind::Interface,
          TS::KnownTypeCode::None, TS::Accessibility::Public, false, true,
          false, std::optional<bool>(true), 0x020004D1u },
        { "System", "Nullable", 1, TS::TypeKind::Struct,
          TS::KnownTypeCode::NullableOfT, TS::Accessibility::Public, false,
          false, true, std::optional<bool>(false), 0x02000162u },
        { "System", "Action", 1, TS::TypeKind::Delegate, TS::KnownTypeCode::None,
          TS::Accessibility::Public, false, false, true,
          std::optional<bool>(true), 0x02000040u },
        { "System", "DayOfWeek", 0, TS::TypeKind::Enum, TS::KnownTypeCode::None,
          TS::Accessibility::Public, false, false, true,
          std::optional<bool>(false), 0x020000D1u },
        { "System.Threading", "Thread", 0, TS::TypeKind::Class,
          TS::KnownTypeCode::None, TS::Accessibility::Public, false, false,
          true, std::optional<bool>(true), 0x02000516u },
        { "System.Runtime.CompilerServices", "RuntimeHelpers", 0,
          TS::TypeKind::Class, TS::KnownTypeCode::None,
          TS::Accessibility::Public, true, true, true,
          std::optional<bool>(true), 0x020008ABu },
        { "System", "TypedReference", 0, TS::TypeKind::Struct,
          TS::KnownTypeCode::TypedReference, TS::Accessibility::Public, false,
          false, true, std::optional<bool>(false), 0x0200014Du },
        { "System", "Guid", 0, TS::TypeKind::Struct, TS::KnownTypeCode::None,
          TS::Accessibility::Public, false, false, true,
          std::optional<bool>(false), 0x020000EBu },
        { "System.Runtime.CompilerServices", "CompilerGeneratedAttribute", 0,
          TS::TypeKind::Class, TS::KnownTypeCode::None,
          TS::Accessibility::Public, false, false, true,
          std::optional<bool>(true), 0x020008ADu },
        // A generic collection whose bases include parameterized interfaces
        // (the TypeSpec InterfaceImpl rows): the kind chain runs the
        // SignatureIsKnownType walk and lands on Class (gold).
        { "System.Collections.ObjectModel", "ReadOnlyCollection", 1,
          TS::TypeKind::Class, TS::KnownTypeCode::None,
          TS::Accessibility::Public, false, false, false,
          std::optional<bool>(true), 0x020004B6u },
        // An internal static class.
        { "Microsoft.Win32", "Win32Native", 0, TS::TypeKind::Class,
          TS::KnownTypeCode::None, TS::Accessibility::Internal, true, true,
          true, std::optional<bool>(true), 0x0200000Eu },
    };

    for (const Expected& e : expected)
    {
        const TS::ITypeDefinition* td =
            f.Type(e.ns, e.name, e.arity);
        ASSERT_NE(td, nullptr);
        EXPECT_EQ(td->Kind(), e.kind)
            << e.ns << "." << e.name << "`" << e.arity;
        EXPECT_EQ(td->KnownTypeCode(), e.knownTypeCode)
            << e.ns << "." << e.name << "`" << e.arity;
        EXPECT_EQ(td->Accessibility(), e.accessibility)
            << e.ns << "." << e.name << "`" << e.arity;
        EXPECT_EQ(td->IsStatic(), e.isStatic)
            << e.ns << "." << e.name << "`" << e.arity;
        EXPECT_EQ(td->IsAbstract(), e.isAbstract)
            << e.ns << "." << e.name << "`" << e.arity;
        EXPECT_EQ(td->IsSealed(), e.isSealed)
            << e.ns << "." << e.name << "`" << e.arity;
        EXPECT_EQ(td->IsReferenceType(), e.isReferenceType)
            << e.ns << "." << e.name << "`" << e.arity;
        EXPECT_EQ(td->MetadataToken(), e.token)
            << e.ns << "." << e.name << "`" << e.arity;
    }
}

TEST(MetadataTypeDefinitionTest, NameFamilyMatchesGold)
{
    MscorlibFixture f;

    // List`1: Name strips the arity, MetadataName keeps it, FullName drops
    // it, ReflectionName keeps it (gold).
    const TS::ITypeDefinition* list =
        f.Type("System.Collections.Generic", "List", 1);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->Name(), "List");
    EXPECT_EQ(list->MetadataName(), "List`1");
    EXPECT_EQ(list->FullName(), "System.Collections.Generic.List");
    EXPECT_EQ(list->ReflectionName(), "System.Collections.Generic.List`1");
    EXPECT_EQ(list->Namespace(), "System.Collections.Generic");
    EXPECT_EQ(list->FullTypeName().ReflectionName(),
              "System.Collections.Generic.List`1");

    // DayOfWeek: the enum underlying type resolves through the compilation's
    // FindType (the gold: ReflectionName "System.Int32", Kind Struct).
    const TS::ITypeDefinition* dayOfWeek = f.Type("System", "DayOfWeek");
    ASSERT_NE(dayOfWeek, nullptr);
    ASSERT_NE(dayOfWeek->EnumUnderlyingType(), nullptr);
    EXPECT_EQ(dayOfWeek->EnumUnderlyingType()->ReflectionName(), "System.Int32");
    EXPECT_EQ(dayOfWeek->EnumUnderlyingType()->Kind(), TS::TypeKind::Struct);

    // A non-enum's underlying type is null.
    EXPECT_EQ(list->EnumUnderlyingType(), nullptr);

    // ToString: the token in 8-digit uppercase hex + " " + the reflection
    // name (gold). The C# object.ToString override is a PLAIN member in
    // the port -- through the concrete class.
    auto toString = [](const TS::ITypeDefinition* td) {
        return dynamic_cast<
            const ILSpy::Decompiler::TypeSystem::Implementation::
                MetadataTypeDefinition*>(td)
            ->ToString();
    };
    EXPECT_EQ(toString(dayOfWeek), "020000D1 System.DayOfWeek");
    EXPECT_EQ(toString(list), "020004DC System.Collections.Generic.List`1");
    EXPECT_EQ(toString(f.Type("System", "Object")), "0200003D System.Object");
}

TEST(MetadataTypeDefinitionTest, NestedChainMatchesGold)
{
    MscorlibFixture f;

    // Win32Native: 53 nested types; the first 8 in table order (gold).
    const TS::ITypeDefinition* win32 =
        f.Type("Microsoft.Win32", "Win32Native");
    ASSERT_NE(win32, nullptr);
    std::vector<const TS::ITypeDefinition*> nested = win32->NestedTypes();
    ASSERT_EQ(nested.size(), 53u);
    const char* expectedNames[] = {
        "SystemTime", "TimeZoneInformation", "DynamicTimeZoneInformation",
        "RegistryTimeZoneInformation", "OSVERSIONINFO", "OSVERSIONINFOEX",
        "SYSTEM_INFO", "SECURITY_ATTRIBUTES",
    };
    for (std::size_t i = 0; i < 8; i++)
        EXPECT_EQ(nested[i]->Name(), expectedNames[i]) << "nested[" << i << "]";

    // The lazily-cached read is identity-stable (the C# LazyInit cache).
    EXPECT_EQ(win32->NestedTypes().size(), 53u);

    // WIN32_FIND_DATA (the second nested type) is a struct with two nested
    // fixed-buffer structs; the innermost's name family pins the nested
    // full/reflection-name forms and the DeclaringTypeDefinition chain.
    const TS::ITypeDefinition* findData = nullptr;
    for (const TS::ITypeDefinition* candidate : nested)
    {
        if (candidate->Name() == "WIN32_FIND_DATA")
            findData = candidate;
    }
    ASSERT_NE(findData, nullptr);
    EXPECT_EQ(findData->Kind(), TS::TypeKind::Struct);
    std::vector<const TS::ITypeDefinition*> fixedBuffers =
        findData->NestedTypes();
    ASSERT_EQ(fixedBuffers.size(), 2u);
    EXPECT_EQ(fixedBuffers[0]->Name(), "<_cFileName>e__FixedBuffer");
    EXPECT_EQ(fixedBuffers[1]->Name(), "<_cAlternateFileName>e__FixedBuffer");

    const TS::ITypeDefinition* fixedBuffer = fixedBuffers[0];
    EXPECT_EQ(fixedBuffer->Kind(), TS::TypeKind::Struct);
    EXPECT_EQ(fixedBuffer->MetadataToken(), 0x02000CF0u);
    EXPECT_EQ(fixedBuffer->FullName(),
              "Microsoft.Win32.Win32Native.WIN32_FIND_DATA."
              "<_cFileName>e__FixedBuffer");
    EXPECT_EQ(fixedBuffer->ReflectionName(),
              "Microsoft.Win32.Win32Native+WIN32_FIND_DATA+"
              "<_cFileName>e__FixedBuffer");
    EXPECT_EQ(
        dynamic_cast<const ILSpy::Decompiler::TypeSystem::Implementation::
                         MetadataTypeDefinition*>(fixedBuffer)
            ->ToString(),
        "02000CF0 Microsoft.Win32.Win32Native+WIN32_FIND_DATA+"
        "<_cFileName>e__FixedBuffer");
    ASSERT_NE(fixedBuffer->DeclaringTypeDefinition(), nullptr);
    EXPECT_EQ(fixedBuffer->DeclaringTypeDefinition()->Name(),
              "WIN32_FIND_DATA");
    // The IType-level DeclaringType aliases the declaring definition.
    ASSERT_NE(fixedBuffer->DeclaringType(), nullptr);
    EXPECT_EQ(fixedBuffer->DeclaringType()->ReflectionName(),
              "Microsoft.Win32.Win32Native+WIN32_FIND_DATA");

    // Enum/Array/Attribute nested counts (gold): Enum 3, Array 5,
    // Attribute 0, Guid 4, RuntimeHelpers 2.
    EXPECT_EQ(f.Type("System", "Enum")->NestedTypes().size(), 3u);
    EXPECT_EQ(f.Type("System", "Array")->NestedTypes().size(), 5u);
    EXPECT_EQ(f.Type("System", "Attribute")->NestedTypes().size(), 0u);
    EXPECT_EQ(f.Type("System", "Guid")->NestedTypes().size(), 4u);
    EXPECT_EQ(f.Type("System.Runtime.CompilerServices", "RuntimeHelpers")
                  ->NestedTypes()
                  .size(),
              2u);
}

// ---------------------------------------------------------------------------
// The type parameters (MetadataTypeParameter's eager surface)
// ---------------------------------------------------------------------------

TEST(MetadataTypeDefinitionTest, TypeParametersMatchGold)
{
    MscorlibFixture f;

    // List`1's T: invariant, index 0, no constraints (gold: 2A00044E `0).
    const TS::ITypeDefinition* list =
        f.Type("System.Collections.Generic", "List", 1);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->TypeParameterCount(), 1);
    std::vector<const TS::ITypeParameter*> tps = list->TypeParameters();
    ASSERT_EQ(tps.size(), 1u);
    EXPECT_EQ(tps[0]->Name(), "T");
    EXPECT_EQ(tps[0]->Index(), 0);
    EXPECT_EQ(tps[0]->OwnerType(), TS::SymbolKind::TypeDefinition);
    EXPECT_EQ(tps[0]->Variance(), TS::VarianceModifier::Invariant);
    EXPECT_FALSE(tps[0]->HasReferenceTypeConstraint());
    EXPECT_FALSE(tps[0]->HasValueTypeConstraint());
    EXPECT_FALSE(tps[0]->HasDefaultConstructorConstraint());
    EXPECT_FALSE(tps[0]->AllowsRefLikeType());
    EXPECT_FALSE(tps[0]->HasUnmanagedConstraint());
    // The port's ITypeParameter is not ISymbol-named; ToString pins the
    // token + reflection-name form (the C# override).
    // (The plain-member ToString is exercised through the concrete class
    // below.)

    // IEnumerable`1's T is covariant; IComparer`1's T is contravariant.
    const TS::ITypeDefinition* enumerable =
        f.Type("System.Collections.Generic", "IEnumerable", 1);
    ASSERT_NE(enumerable, nullptr);
    ASSERT_EQ(enumerable->TypeParameters().size(), 1u);
    EXPECT_EQ(enumerable->TypeParameters()[0]->Variance(),
              TS::VarianceModifier::Covariant);
    const TS::ITypeDefinition* comparer =
        f.Type("System.Collections.Generic", "IComparer", 1);
    ASSERT_NE(comparer, nullptr);
    ASSERT_EQ(comparer->TypeParameters().size(), 1u);
    EXPECT_EQ(comparer->TypeParameters()[0]->Variance(),
              TS::VarianceModifier::Contravariant);

    // Action`1's T is contravariant too (a delegate's parameter flows in).
    const TS::ITypeDefinition* action = f.Type("System", "Action", 1);
    ASSERT_NE(action, nullptr);
    ASSERT_EQ(action->TypeParameters().size(), 1u);
    EXPECT_EQ(action->TypeParameters()[0]->Variance(),
              TS::VarianceModifier::Contravariant);

    // Nullable`1's T carries the value-type + default-constructor
    // constraints (gold: val=True ctor=True).
    const TS::ITypeDefinition* nullable = f.Type("System", "Nullable", 1);
    ASSERT_NE(nullable, nullptr);
    ASSERT_EQ(nullable->TypeParameters().size(), 1u);
    EXPECT_TRUE(nullable->TypeParameters()[0]->HasValueTypeConstraint());
    EXPECT_TRUE(nullable->TypeParameters()[0]->HasDefaultConstructorConstraint());

    // Dictionary`2's TKey/TValue: the gold tokens + the C# ToString render
    // (the `$"{token:X8} {ReflectionName}"` form over the CONCRETE class --
    // ITypeParameter itself carries no MetadataToken member).
    const TS::ITypeDefinition* dictionary =
        f.Type("System.Collections.Generic", "Dictionary", 2);
    ASSERT_NE(dictionary, nullptr);
    ASSERT_EQ(dictionary->TypeParameters().size(), 2u);
    EXPECT_EQ(dictionary->TypeParameters()[0]->Name(), "TKey");
    EXPECT_EQ(dictionary->TypeParameters()[1]->Name(), "TValue");
    auto* keyParameter = dynamic_cast<const ILSpy::Decompiler::TypeSystem::
        Implementation::MetadataTypeParameter*>(
            dictionary->TypeParameters()[0]);
    ASSERT_NE(keyParameter, nullptr);
    EXPECT_EQ(keyParameter->MetadataToken(), 0x2A00042Du);
    EXPECT_EQ(keyParameter->ToString(), "2A00042D `0");
    EXPECT_EQ(keyParameter->GetHashCode(), keyParameter->GetHashCode());
    // The IType-level Equals routes through the handle check: the same
    // parameter equals itself, a different type does not.
    const TS::IType* keyAsType = dictionary->TypeParameters()[0];
    EXPECT_TRUE(keyAsType->Equals(*keyAsType));
    const TS::IType* valueAsType = dictionary->TypeParameters()[1];
    EXPECT_FALSE(keyAsType->Equals(*valueAsType));
}

TEST(MetadataTypeDefinitionTest, NestedTypeParametersAliasTheOuter)
{
    MscorlibFixture f;

    // Dictionary`2+KeyCollection has NO own GenericParam rows: its type
    // parameters are the OUTER's aliases (the C# Create copyFromOuter arm;
    // the gold: tpc=2, aliasSame=True, the same 2A00042D/2A00042E tokens).
    const TS::ITypeDefinition* dictionary =
        f.Type("System.Collections.Generic", "Dictionary", 2);
    ASSERT_NE(dictionary, nullptr);
    std::vector<const TS::ITypeDefinition*> nested = dictionary->NestedTypes();
    ASSERT_EQ(nested.size(), 4u);
    EXPECT_EQ(nested[0]->Name(), "Entry");
    EXPECT_EQ(nested[1]->Name(), "Enumerator");
    EXPECT_EQ(nested[2]->Name(), "KeyCollection");
    EXPECT_EQ(nested[3]->Name(), "ValueCollection");

    const TS::ITypeDefinition* keyCollection = nested[2];
    EXPECT_EQ(keyCollection->MetadataToken(), 0x02000BE2u);
    EXPECT_EQ(keyCollection->TypeParameterCount(), 2);
    ASSERT_EQ(keyCollection->TypeParameters().size(), 2u);
    // THE ALIAS: the same ITypeParameter instances as the outer's (the C#
    // ReferenceEquals gold).
    EXPECT_EQ(keyCollection->TypeParameters()[0],
              dictionary->TypeParameters()[0]);
    EXPECT_EQ(keyCollection->TypeParameters()[1],
              dictionary->TypeParameters()[1]);
    EXPECT_EQ(keyCollection->TypeParameters()[0]->Name(), "TKey");
    EXPECT_EQ(keyCollection->TypeParameters()[0]->Index(), 0);
    EXPECT_EQ(keyCollection->TypeParameters()[0]->Variance(),
              TS::VarianceModifier::Invariant);
}

// ---------------------------------------------------------------------------
// The ref/readonly struct flags over CoreLib + the extension classes
// ---------------------------------------------------------------------------

TEST(MetadataTypeDefinitionTest, RefAndReadOnlyStructFlagsMatchGoldOverCoreLib)
{
    if (!FileExists(CoreLibPath()))
        GTEST_SKIP() << "no local CoreLib fixture";

    TM::MetadataFile file(CoreLibPath());
    CacheCompilation compilation;
    TS::MetadataModule module(compilation, &file, TS::TypeSystemOptions::Default);
    compilation.SetMainModule(&module);

    auto type = [&module](const char* ns, const char* name, int arity) {
        const TS::ITypeDefinition* definition = module.GetTypeDefinition(
            TS::TopLevelTypeName(ns, name, arity));
        EXPECT_NE(definition, nullptr) << ns << "." << name;
        return definition;
    };

    // Span`1: byRefLike + readOnly; ReadOnlySpan`1 likewise (gold).
    const TS::ITypeDefinition* span = type("System", "Span", 1);
    ASSERT_NE(span, nullptr);
    EXPECT_EQ(span->Kind(), TS::TypeKind::Struct);
    EXPECT_EQ(span->KnownTypeCode(), TS::KnownTypeCode::SpanOfT);
    EXPECT_TRUE(span->IsByRefLike());
    EXPECT_TRUE(span->IsReadOnly());
    EXPECT_EQ(span->MetadataToken(), 0x020001C7u);

    const TS::ITypeDefinition* readOnlySpan = type("System", "ReadOnlySpan", 1);
    ASSERT_NE(readOnlySpan, nullptr);
    EXPECT_EQ(readOnlySpan->KnownTypeCode(),
              TS::KnownTypeCode::ReadOnlySpanOfT);
    EXPECT_TRUE(readOnlySpan->IsByRefLike());
    EXPECT_TRUE(readOnlySpan->IsReadOnly());

    // CoreLib's Int32 is a readonly struct; .NET Framework 4.8's is not
    // (gold: readOnly=True vs the mscorlib False).
    const TS::ITypeDefinition* int32 = type("System", "Int32", 0);
    ASSERT_NE(int32, nullptr);
    EXPECT_TRUE(int32->IsReadOnly());

    // ArgIterator: byRefLike without readOnly (gold).
    const TS::ITypeDefinition* argIterator = type("System", "ArgIterator", 0);
    ASSERT_NE(argIterator, nullptr);
    EXPECT_TRUE(argIterator->IsByRefLike());
    EXPECT_FALSE(argIterator->IsReadOnly());

    // CoreLib's Action`1 carries the AllowByRefLike bit (gold:
    // refLike=True; the mscorlib 4.8 fixture is False).
    const TS::ITypeDefinition* action = type("System", "Action", 1);
    ASSERT_NE(action, nullptr);
    ASSERT_EQ(action->TypeParameters().size(), 1u);
    EXPECT_TRUE(action->TypeParameters()[0]->AllowsRefLikeType());
}

TEST(MetadataTypeDefinitionTest, HasExtensionsMatchesGold)
{
    MscorlibFixture f;

    // The mscorlib extension classes (the gold scan): static classes whose
    // [Extension] attribute the option-gated HasKnownAttribute scan finds.
    const char* extensionClasses[] = {
        "Microsoft.Reflection.ReflectionExtensions",
        "System.TupleExtensions",
        "System.Globalization.GlobalizationExtensions",
        "System.Threading.WaitHandleExtensions",
        "System.Reflection.CustomAttributeExtensions",
    };
    for (const char* fullName : extensionClasses)
    {
        std::string ns(fullName);
        std::string name = ns.substr(ns.rfind('.') + 1);
        ns.resize(ns.rfind('.'));
        const TS::ITypeDefinition* td = f.Type(ns.c_str(), name.c_str());
        ASSERT_NE(td, nullptr);
        EXPECT_TRUE(td->HasExtensions()) << fullName;
    }

    // Non-extension types are false (gold).
    EXPECT_FALSE(f.Type("System", "Math")->HasExtensions());
    EXPECT_FALSE(f.Type("System", "String")->HasExtensions());

    // System.Core's System.Linq.Enumerable: the Extends column is a mscorlib
    // TypeRef (the IsKnownType TypeReference arm -- the kind chain reads it)
    // and the class-level [Extension] is present (gold: kind=Class,
    // hasExtensions=True, Public, static, 0x0200014F).
    if (FileExists(SystemCorePath()))
    {
        TM::MetadataFile file(SystemCorePath());
        CacheCompilation compilation;
        TS::MetadataModule module(compilation, &file,
                                  TS::TypeSystemOptions::Default);
        compilation.SetMainModule(&module);
        const TS::ITypeDefinition* enumerable = module.GetTypeDefinition(
            TS::TopLevelTypeName("System.Linq", "Enumerable"));
        ASSERT_NE(enumerable, nullptr);
        EXPECT_EQ(enumerable->Kind(), TS::TypeKind::Class);
        EXPECT_TRUE(enumerable->HasExtensions());
        EXPECT_EQ(enumerable->Accessibility(), TS::Accessibility::Public);
        EXPECT_TRUE(enumerable->IsStatic());
        EXPECT_EQ(enumerable->MetadataToken(), 0x0200014Fu);
        // The TypeRef base arm: System.Object (a mscorlib TypeRef from
        // System.Core's perspective) is neither Enum nor ValueType nor
        // MulticastDelegate.
        EXPECT_EQ(enumerable->IsReferenceType(), std::optional<bool>(true));
    }
}

// ---------------------------------------------------------------------------
// The entity cache, equality, and the deferral contracts
// ---------------------------------------------------------------------------

TEST(MetadataTypeDefinitionTest, EntityCacheAndEquality)
{
    MscorlibFixture f;

    // Two lookups of System.String return the SAME instance (the C#
    // LazyInit entity cache).
    const TS::ITypeDefinition* first = f.Type("System", "String");
    const TS::ITypeDefinition* second =
        f.module.GetDefinition(0x02000073u);
    EXPECT_EQ(first, second);

    // The IType-level Equals: same handle + same MetadataFile.
    EXPECT_TRUE(first->Equals(*second));
    EXPECT_TRUE(first->Equals(*first));
    const TS::ITypeDefinition* object_ = f.Type("System", "Object");
    EXPECT_FALSE(first->Equals(*object_));

    // The nil token returns null; a row past the table throws the exact
    // message.
    EXPECT_EQ(f.module.GetDefinition(0x02000000u), nullptr);
    try
    {
        f.module.GetDefinition(0x02000000u + 4096);
        FAIL() << "GetDefinition past the table end must throw";
    }
    catch (const std::out_of_range& ex)
    {
        EXPECT_STREQ(ex.what(), "Handle with invalid row number.");
    }

    // The Uncached option: two lookups construct DIFFERENT instances (and
    // both stay alive through the module's keep-alive registry).
    CacheCompilation uncachedCompilation;
    TS::MetadataModule uncached(uncachedCompilation, &f.file,
                                TS::TypeSystemOptions::Default
                                    | TS::TypeSystemOptions::Uncached);
    uncachedCompilation.SetMainModule(&uncached);
    const TS::ITypeDefinition* a = uncached.GetDefinition(0x02000073u);
    const TS::ITypeDefinition* b = uncached.GetDefinition(0x02000073u);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_TRUE(a->Equals(*b));
    // The Napollyon arm: the UNCACHED GetDefinition does NOT range-check
    // (the C# constructs and lets the ctor's row read throw).
    try
    {
        uncached.GetDefinition(0x02000000u + 4096);
        FAIL() << "the uncached ctor must throw on the bogus row";
    }
    catch (const std::out_of_range&)
    {
    }
}

TEST(MetadataTypeDefinitionTest, DeferralContracts)
{
    MscorlibFixture f;
    const TS::ITypeDefinition* string_ = f.Type("System", "String");
    ASSERT_NE(string_, nullptr);

    // The property/event members and the attribute snapshot are the loud
    // deferrals of the remaining slices (DirectBaseTypes landed with the
    // ResolveType pair -- the ResolveType_Test suite pins it; Fields landed
    // with the MetadataField family -- the MetadataField_Test suite pins
    // it; Methods landed with the FakeMember dummy-constructor slice -- the
    // MemberEnumeration_Test suite pins the whole-corpus sweeps).
    EXPECT_THROW(string_->Members(), std::logic_error);
    EXPECT_EQ(string_->Fields().size(), 8u);
    EXPECT_EQ(string_->Methods().size(), 191u);
    EXPECT_THROW(string_->Properties(), std::logic_error);
    EXPECT_THROW(string_->Events(), std::logic_error);
    EXPECT_THROW(string_->GetAttributes(), std::logic_error);
    EXPECT_THROW(string_->HasAttribute(TS::KnownAttribute::Obsolete),
                 std::logic_error);
    EXPECT_THROW(string_->GetAttribute(TS::KnownAttribute::Obsolete),
                 std::logic_error);
    // IsRecord landed (the raw method-name scan): String is not a record.
    EXPECT_FALSE(string_->IsRecord());

    // The Void early-exit arms return the empty list without touching the
    // member family (the C# `if (Kind == TypeKind.Void) return
    // EmptyList<...>.Instance;`).
    const TS::ITypeDefinition* void_ = f.Type("System", "Void");
    ASSERT_NE(void_, nullptr);
    EXPECT_EQ(void_->Kind(), TS::TypeKind::Void);
    EXPECT_TRUE(void_->GetMethods().empty());
    EXPECT_TRUE(void_->GetConstructors().empty());
    EXPECT_TRUE(void_->GetProperties().empty());
    EXPECT_TRUE(void_->GetFields().empty());
    EXPECT_TRUE(void_->GetEvents().empty());
    EXPECT_TRUE(void_->GetMembers().empty());
    EXPECT_TRUE(void_->GetAccessors().empty());

    // ExtensionInfo: the null arms are real -- a non-extension type returns
    // null without touching the member family (the C# `if (!HasExtensions)
    // return null;`).
    EXPECT_EQ(string_->ExtensionInfo(), nullptr);

    // The ITypeParameter deferrals: TypeConstraints / NullabilityConstraint
    // / GetAttributes throw (the ResolveType / NullableAttribute decode /
    // AttributeListBuilder gates).
    const TS::ITypeDefinition* list =
        f.Type("System.Collections.Generic", "List", 1);
    ASSERT_NE(list, nullptr);
    const TS::ITypeParameter* parameter = list->TypeParameters()[0];
    ASSERT_NE(parameter, nullptr);
    EXPECT_THROW(parameter->TypeConstraints(), std::logic_error);
    EXPECT_THROW(parameter->NullabilityConstraint(), std::logic_error);
    EXPECT_THROW(parameter->GetAttributes(), std::logic_error);
    // DirectBaseTypes reads TypeConstraints (the AbstractTypeParameter
    // projection) -- the same deferral.
    EXPECT_THROW(parameter->DirectBaseTypes(), std::logic_error);

    // GetNestedTypes: the (IgnoreInheritedMembers | ReturnMemberDefinitions)
    // short-circuit arm is REAL (the nested list as ITypePtr); the routed
    // arms throw the GetMembersHelper deferral.
    const TS::ITypeDefinition* win32 =
        f.Type("Microsoft.Win32", "Win32Native");
    ASSERT_NE(win32, nullptr);
    std::vector<TS::ITypePtr> viaShortCircuit = win32->GetNestedTypes(
        nullptr, TS::GetMemberOptions::IgnoreInheritedMembers
                     | TS::GetMemberOptions::ReturnMemberDefinitions);
    EXPECT_EQ(viaShortCircuit.size(), 53u);
    // The filter drops non-matching entries (the C# GetFiltered).
    std::vector<TS::ITypePtr> viaFilter = win32->GetNestedTypes(
        [](const TS::ITypeDefinition* td) { return td->Name() == "SystemTime"; },
        TS::GetMemberOptions::IgnoreInheritedMembers
            | TS::GetMemberOptions::ReturnMemberDefinitions);
    ASSERT_EQ(viaFilter.size(), 1u);
    EXPECT_EQ(viaFilter[0]->Name(), "SystemTime");
    EXPECT_THROW(win32->GetNestedTypes(
                     nullptr, TS::GetMemberOptions::None),
                 std::logic_error);
    EXPECT_THROW((win32->GetNestedTypes(std::vector<TS::ITypePtr>{}, nullptr,
                                        TS::GetMemberOptions::None)),
                 std::logic_error);
}

TEST(MetadataTypeDefinitionTest, NullableContextIsObliviousOverMscorlib)
{
    MscorlibFixture f;

    // No .NET Framework 4.8 assembly carries [NullableContext] attributes,
    // so the real engine's NullableContext is Oblivious everywhere over
    // mscorlib (the gold: nullableContext=Oblivious) -- the port's deferred
    // decode returns the same. (CoreLib types carry real contexts; that
    // divergence is documented in the header, convention (d).)
    EXPECT_EQ(f.Type("System", "String")->NullableContext(),
              TS::Nullability::Oblivious);
}
