// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE, NONINFRINGEMENT, OR AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the SecurityDeclarationDecoder port (ReflectionDisassembler.cs
// lines 487-675, the nested ICustomAttributeTypeProvider<(PrimitiveTypeCode
// Code, string Name)> implementation the custom-attribute-blob and
// permission-set decode paths instantiate the decoder template with), plus
// the PrimitiveTypeCodeToString spellings and the ReflectionDisassembler
// AssemblyResolver property. The fixtures: the synthetic manifest (an
// NS.MyEnum enum with an Int32 underlying type, NS.Attr/NS.GAttr/NS.Holder
// non-enums, three mscorlib-scoped TypeRefs, the "mscorlib, Version=4.0.0.0,
// Culture=neutral, PublicKeyToken=null" AssemblyRef) driven through a
// counting stub IAssemblyResolver; the staged-mscorlib arms (the
// resolved-found / resolved-enum paths) are gated on ILSPY_TEST_MSCORLIB
// and skip when the fixture is absent.

#include <cstdlib>
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Disassembler/SecurityDeclarationDecoder.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "TestFixtures/CadDecoderGold.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace TM = ILSpy::Decompiler::Metadata;
namespace TD = ILSpy::Decompiler::Disassembler;
namespace TO = ILSpy::Decompiler::Output;

// The C# tuple's zero code (the pair for a type that carries no underlying
// primitive).
constexpr TM::PrimitiveTypeCode kNoCode = static_cast<TM::PrimitiveTypeCode>(0);

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

bool FileAvailable(const char* path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Writes the embedded manifest to a temp file (MetadataFile needs a real
// file) -- the AssemblyIdentityFixtures WriteTempAssembly convention.
std::string WriteSynthManifest() {
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "SecDeclSynth_test.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(ILSpy::Tests::kCadSynthManifest, 1,
        ILSpy::Tests::kCadSynthManifestSize, out);
    std::fclose(out);
    return path.string();
}

// The counting stub resolver: resolves every reference whose short name is
// "mscorlib" to the caller-owned file (or reports a miss when the file is
// null), and counts the Resolve calls. ResolveModule is unreachable from
// the SecurityDeclarationDecoder paths (the C# ResolveType never calls it)
// and returns null.
class StubResolver final : public TM::IAssemblyResolver {
public:
    const TM::MetadataFile* Resolve(
        const TM::IAssemblyReference& reference) const override {
        resolveCalls++;
        lastFullName = reference.FullName();
        if (mscorlib == nullptr || reference.Name() != "mscorlib")
            return nullptr;
        return mscorlib;
    }

    const TM::MetadataFile* ResolveModule(const TM::MetadataFile&,
        const std::string&) const override {
        return nullptr;
    }

    const TM::MetadataFile* mscorlib = nullptr;
    mutable int resolveCalls = 0;
    mutable std::string lastFullName;
};

TEST(SecurityDeclarationDecoderTest, PrimitiveArmsAndPrimitiveTypeCodeToString)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    StubResolver resolver;
    TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);

    EXPECT_EQ(decoder.GetPrimitiveType(TM::PrimitiveTypeCode::Int32),
        (TD::SecurityDeclarationType{TM::PrimitiveTypeCode::Int32,
            std::nullopt}));
    EXPECT_EQ(decoder.GetSystemType(),
        (TD::SecurityDeclarationType{kNoCode, std::string("type")}));
    EXPECT_EQ(decoder.GetUnderlyingEnumType(
                  TD::SecurityDeclarationType{TM::PrimitiveTypeCode::UInt64,
                      std::string("enum X.Y")}),
        TM::PrimitiveTypeCode::UInt64);
    EXPECT_TRUE(decoder.IsSystemType(
        TD::SecurityDeclarationType{kNoCode, std::string("type")}));
    EXPECT_FALSE(decoder.IsSystemType(
        TD::SecurityDeclarationType{kNoCode, std::string("System.Type")}));

    // The C# GetSZArrayType: `(Item1, (Item2 ??
    // PrimitiveTypeCodeToString(Item1)) + "[]")`.
    EXPECT_EQ(decoder.GetSZArrayType(
                  TD::SecurityDeclarationType{TM::PrimitiveTypeCode::Int32,
                      std::nullopt}),
        (TD::SecurityDeclarationType{TM::PrimitiveTypeCode::Int32,
            std::string("int32[]")}));
    EXPECT_EQ(decoder.GetSZArrayType(
                  TD::SecurityDeclarationType{kNoCode,
                      std::string("enum NS.MyEnum")}),
        (TD::SecurityDeclarationType{kNoCode, std::string("enum NS.MyEnum[]")}));

    // The PrimitiveTypeCodeToString spellings (the private static of the
    // ReflectionDisassembler the SZArray name falls back to).
    EXPECT_EQ(TD::PrimitiveTypeCodeToString(TM::PrimitiveTypeCode::Boolean),
        "bool");
    EXPECT_EQ(TD::PrimitiveTypeCodeToString(TM::PrimitiveTypeCode::Single),
        "float32");
    EXPECT_EQ(TD::PrimitiveTypeCodeToString(TM::PrimitiveTypeCode::String),
        "string");
    EXPECT_EQ(TD::PrimitiveTypeCodeToString(TM::PrimitiveTypeCode::Void),
        "unknown");
}

TEST(SecurityDeclarationDecoderTest, GetTypeFromDefinitionResolvesEnums)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    StubResolver resolver;
    TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);

    // NS.MyEnum (row 2): the enum arm -- the underlying code plus the
    // "enum " prefix over the full type name.
    EXPECT_EQ(decoder.GetTypeFromDefinition(manifest, 0x02000002, 0),
        (TD::SecurityDeclarationType{TM::PrimitiveTypeCode::Int32,
            std::string("enum NS.MyEnum")}));

    // NS.Attr (row 3) and NS.GAttr (row 4): the non-enum arm -- the zero
    // code and the plain full name.
    EXPECT_EQ(decoder.GetTypeFromDefinition(manifest, 0x02000003, 0),
        (TD::SecurityDeclarationType{kNoCode, std::string("NS.Attr")}));
    EXPECT_EQ(decoder.GetTypeFromDefinition(manifest, 0x02000004, 0),
        (TD::SecurityDeclarationType{kNoCode, std::string("NS.GAttr")}));
}

TEST(SecurityDeclarationDecoderTest, GetTypeFromReferenceComposesTheScope)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;

    // The unresolved arm: the resolver reports a miss for mscorlib, so the
    // module stays null, the enum check is skipped, and the name keeps the
    // assembly-qualified composition (the TypeRef's full name plus the
    // AssemblyRef's full assembly name from the GetDeclaringModule walk).
    {
        StubResolver resolver;  // mscorlib == nullptr: every Resolve misses
        TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);
        // The System.Type TypeRef (row 3).
        EXPECT_EQ(decoder.GetTypeFromReference(manifest, 0x01000003, 0),
            (TD::SecurityDeclarationType{
                kNoCode,
                std::string("System.Type, mscorlib, Version=4.0.0.0, "
                            "Culture=neutral, PublicKeyToken=null")}));
    }

    // The resolved-miss arm: the resolver hands back the manifest itself as
    // "mscorlib" -- the manifest has no System.Type TypeDef (only the
    // TypeRef), so FindType misses and the nil-handle IsEnum read answers
    // false (the C# GetTypeDefinition over the nil row reads the unchecked
    // pre-table bytes -- a garbage Extends token that is not System.Enum;
    // the port's row-0 read is the graceful false) -- so the composed name
    // is kept and the code stays zero, the same value as the unresolved arm
    // (the arms differ only in the resolver's call count).
    {
        StubResolver resolver;
        resolver.mscorlib = &manifest;
        TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);
        EXPECT_EQ(decoder.GetTypeFromReference(manifest, 0x01000003, 0),
            (TD::SecurityDeclarationType{
                kNoCode,
                std::string("System.Type, mscorlib, Version=4.0.0.0, "
                            "Culture=neutral, PublicKeyToken=null")}));
        EXPECT_EQ(resolver.resolveCalls, 1);
    }
}

TEST(SecurityDeclarationDecoderTest, GetTypeFromSerializedNameResolvesAndThrows)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;

    // The null resolver always throws (the C# first arm).
    {
        TD::SecurityDeclarationDecoder decoder(output, nullptr, manifest);
        EXPECT_THROW(decoder.GetTypeFromSerializedName("NS.MyEnum"),
            TM::EnumUnderlyingTypeResolveException);
    }

    StubResolver resolver;
    TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);

    // The current-module hit: the unqualified name resolves through FindType
    // in the decoder's own module -- the enum arm.
    EXPECT_EQ(decoder.GetTypeFromSerializedName("NS.MyEnum"),
        (TD::SecurityDeclarationType{TM::PrimitiveTypeCode::Int32,
            std::string("enum NS.MyEnum")}));

    // The unqualified miss: the current module does not carry the type and
    // the resolver reports no mscorlib -- the nil-handle throw.
    EXPECT_THROW(decoder.GetTypeFromSerializedName("NoSuch.Type"),
        TM::EnumUnderlyingTypeResolveException);

    // The qualified miss: the resolver cannot resolve "SomeOther", so the
    // C# ResolveType falls back to the CURRENT module (containingModule is
    // null through the miss, so FindType runs over the decoder's module),
    // where NS.Attr IS found -- the original name (the qualifier included)
    // returns with the zero code.
    resolver.lastFullName.clear();
    EXPECT_EQ(decoder.GetTypeFromSerializedName("NS.Attr, SomeOther"),
        (TD::SecurityDeclarationType{kNoCode,
            std::string("NS.Attr, SomeOther")}));

    // The qualified hit: the assembly part resolves to the manifest itself
    // and the type is found there -- the non-enum arm returns the ORIGINAL
    // name (the qualifier included).
    {
        StubResolver hitResolver;
        hitResolver.mscorlib = &manifest;
        TD::SecurityDeclarationDecoder hitDecoder(output, &hitResolver,
            manifest);
        EXPECT_EQ(hitDecoder.GetTypeFromSerializedName("NS.Attr, mscorlib"),
            (TD::SecurityDeclarationType{kNoCode,
                std::string("NS.Attr, mscorlib")}));
    }
}

TEST(SecurityDeclarationDecoderTest, TryResolveMscorlibCachesTheResolvedModule)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;

    // The unresolved mscorlib is not cached: every unqualified miss
    // re-resolves (the C# null result leaves the field null).
    {
        StubResolver resolver;
        TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);
        EXPECT_THROW(decoder.GetTypeFromSerializedName("NoSuch.One"),
            TM::EnumUnderlyingTypeResolveException);
        EXPECT_THROW(decoder.GetTypeFromSerializedName("NoSuch.Two"),
            TM::EnumUnderlyingTypeResolveException);
        EXPECT_EQ(resolver.resolveCalls, 2);
    }

    // The resolved mscorlib is cached: the first unqualified miss resolves
    // "mscorlib" and the field holds it; the second miss reuses it (one
    // Resolve call). The lastFullName pins the AssemblyNameReference.Parse
    // composition the resolver received.
    {
        StubResolver resolver;
        resolver.mscorlib = &manifest;
        TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);
        EXPECT_THROW(decoder.GetTypeFromSerializedName("System.String"),
            TM::EnumUnderlyingTypeResolveException);
        EXPECT_THROW(decoder.GetTypeFromSerializedName("System.Int32"),
            TM::EnumUnderlyingTypeResolveException);
        EXPECT_EQ(resolver.resolveCalls, 1);
        // The C# AssemblyNameReference.FullName always appends the version
        // (the `Version ?? UniversalAssemblyResolver.ZeroVersion` default,
        // four fields).
        EXPECT_EQ(resolver.lastFullName,
            "mscorlib, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
    }
}

TEST(SecurityDeclarationDecoderTest, StagedMscorlibResolvesRealTypes)
{
    if (!FileAvailable(MscorlibPath()))
        GTEST_SKIP() << "mscorlib fixture not available";

    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TM::MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    TO::PlainTextOutput output;

    StubResolver resolver;
    resolver.mscorlib = &mscorlib;
    TD::SecurityDeclarationDecoder decoder(output, &resolver, manifest);

    // The unqualified mscorlib fallback hit: the current module misses,
    // TryResolveMscorlib resolves the staged mscorlib, and FindType finds
    // the non-enum System.String -- the zero code and the original name.
    EXPECT_EQ(decoder.GetTypeFromSerializedName("System.String"),
        (TD::SecurityDeclarationType{kNoCode, std::string("System.String")}));

    // The mscorlib enum hit: System.AttributeTargets is an Int32 enum.
    EXPECT_EQ(decoder.GetTypeFromSerializedName("System.AttributeTargets"),
        (TD::SecurityDeclarationType{TM::PrimitiveTypeCode::Int32,
            std::string("enum System.AttributeTargets")}));

    // The resolved TypeRef hit: the System.Type TypeRef's assembly-qualified
    // name resolves through the staged mscorlib -- found, not an enum.
    EXPECT_EQ(decoder.GetTypeFromReference(manifest, 0x01000003, 0),
        (TD::SecurityDeclarationType{
            kNoCode,
            std::string("System.Type, mscorlib, Version=4.0.0.0, "
                        "Culture=neutral, PublicKeyToken=null")}));
}

TEST(SecurityDeclarationDecoderTest, ReflectionDisassemblerAssemblyResolverProperty)
{
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    // The C# `public IAssemblyResolver AssemblyResolver { get; set; }`
    // defaults to null; the caller owns the pointed-at resolver (the
    // EntityProcessor pointer-property convention).
    EXPECT_EQ(disassembler.AssemblyResolver(), nullptr);
    StubResolver resolver;
    disassembler.AssemblyResolver(&resolver);
    EXPECT_EQ(disassembler.AssemblyResolver(), &resolver);
}

} // namespace
