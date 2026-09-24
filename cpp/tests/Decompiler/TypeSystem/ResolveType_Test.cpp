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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the MetadataModule.ResolveType + MetadataTypeDefinition.DirectBaseTypes
// port (MetadataModule.cs lines 371-397 + MetadataTypeDefinition.cs lines 340-380):
// the top-byte table dispatch of ResolveType (TypeDefinition / TypeReference /
// TypeSpecification / ExportedType / nil / the default arm) and the
// LazyInit-cached Extends + InterfaceImpl resolution of DirectBaseTypes (the
// interface-to-Object fallback, the per-row attribute feed, the
// BadImageFormatException catch arm).
//
// Every expectation is gold-pinned against the REAL ICSharpCode.Decompiler 11.0
// driven over the identical fixtures (the C:/temp-probe/RtProbe gold probe):
//   * the DirectBaseTypes matrix over mscorlib (String's 8 bases, List`1's 10,
//     the interface fallback, the enum/struct/delegate chains, the nested
//     Dictionary`2) and its identity contracts (the base IS the module's
//     cached Object definition; the fallback Object IS FindType(Object); two
//     reads return the same elements);
//   * the cross-module shapes over System.dll alone (the unresolvable
//     mscorlib TypeRef -> UnknownType) and System.dll + mscorlib (the
//     resolved base IS the mscorlib module's definition);
//   * ResolveType's arms: nil -> the SpecialType null object ("?"), the
//     TypeDef entity-cache identity, the TypeSpec generic instantiation
//     (FileSystemEnumerableIterator`1's Iterator`1<!0> base), the TypeRef
//     resolution and miss, the facade's String forwarder, and the
//     "Not a type handle" / "Handle with invalid row number." throws;
//   * the corrupt-Extends catch arm over a byte-patched synthetic manifest
//     (the Extends coded index re-pointed past the 2-row TypeDef table ->
//     the swallowed throw renders SpecialType.UnknownType).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include <cstdlib>
#include "Decompiler/TypeSystem/GenericContext.hpp"
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
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
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
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* SystemPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

#if defined(_WIN32)
const char* FacadePath() {
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
}
#endif

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

// A compilation whose main module is settable and whose FindType routes
// through the ported KnownTypeCache over the module (the
// MetadataTypeDefinition_Test CacheCompilation shape -- the real engine
// resolves the interface-fallback Object through the same cache).
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

// A compilation whose module list is settable (the
// MetadataModuleResolution_Test ModulesCompilation shape) for the
// multi-module drives.
class ModulesCompilation : public TS::ICompilation {
public:
    void SetModules(std::vector<const TS::IModule*> modules)
    {
        modules_ = std::move(modules);
    }

    // --- ICompilation ---
    const TS::IModule& MainModule() const override { return *modules_[0]; }
    std::vector<const TS::IModule*> Modules() const override
    {
        return modules_;
    }
    std::vector<const TS::IModule*> ReferencedModules() const override
    {
        return std::vector<const TS::IModule*>(modules_.begin() + 1,
                                               modules_.end());
    }
    const TS::INamespace& RootNamespace() const override
    {
        return modules_[0]->RootNamespace();
    }
    const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const TS::IType& FindType(TS::KnownTypeCode) const override
    {
        return knownType_;
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
    std::vector<const TS::IModule*> modules_;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
    TS::KnownType knownType_{ TS::KnownTypeCode::Object };
};

// One module over one file, bound to a compilation (the file outlives the
// module -- the caller-owns-the-file contract).
struct ModuleEntry {
    TM::MetadataFile file;
    TS::MetadataModule module;

    ModuleEntry(const std::string& filePath,
                const TS::ICompilation& compilation)
        : file(filePath), module(compilation, &file,
                                 TS::TypeSystemOptions::Default)
    {
    }
};

// A bundle of modules sharing one compilation (the probe's
// SimpleCompilation(main, referenced...) shape).
struct CompilationBundle {
    ModulesCompilation compilation;
    std::vector<std::unique_ptr<ModuleEntry>> entries;
    std::vector<const TS::IModule*> modules;

    TS::MetadataModule& Add(const std::string& filePath)
    {
        entries.push_back(
            std::make_unique<ModuleEntry>(filePath, compilation));
        modules.push_back(&entries.back()->module);
        return entries.back()->module;
    }

    void Seal() { compilation.SetModules(std::move(modules)); }
};

// The byte-patched synthetic manifest (2048 bytes): two TypeDefs ("Rt.Bad"
// extends the System.Object TypeRef, "Rt.Plain" a nil Extends) whose first
// row's Extends coded index is re-pointed at a TypeDef rid (0x3FFF) past the
// 2-row table -- built with the real .NET 10 MetadataBuilder /
// ManagedPEBuilder by the RtProbe gold probe and patched through the
// reflected TypeDef-table column layout (the FullAsmNameProbe recipe). The
// original Extends coded value is 0x0005 (the TypeRef row 1).
const char* kCorruptExtendsHex =
    "4d5a90000300000004000000ffff0000b800000000000000400000000000000000000000000000000000000000000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd21546869732070726f6772616d2063616e6e6f742062652072756e20696e20444f53206d6f64652e0d0d0a2400000000000000504500004c010200c7969c6a0000000000000000e00002200b01300000040000000200000000000022220000002000000040000000004000002000000002000004000000000000000400000000000000006000000002000000000000030040850000100000100000000010000010000000000000100000000000000000000000d02100004f000000000000000000000000000000000000000000000000000000004000000c00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000200000080000000000000000000000082000004800000000000000000000002e7465787400000028020000002000000004000000020000000000000000000000000000200000602e72656c6f6300000c000000004000000002000000060000000000000000000000000000400000420000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000042200000000000048000000020005005020000000010000010000000000000000000000000000005021000080000000000000000000000000000000000000000000000000000000000000000000000042534a4201000100000000000c00000076342e302e33303331390000000005006c00000054000000237e0000c00000002800000023537472696e677300000000e80000000400000023555300ec000000100000002347554944000000fc0000000400000023426c6f620000000000000002000001070000000000000000fa013300160000010000000100000002000000000005000100000000000000210011000100000001001e00fcff010001000100000018001e00000001000100000000000042616400636f72727570742e646c6c0053797374656d00506c61696e005274004f626a6563740000000000443322116655887799aabbccddeeff00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f821000000000000000000001222000000200000000000000000000000000000000000000000000004220000000000000000000000005f436f72446c6c4d61696e006d73636f7265652e646c6c0000000000ff250020400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002000000c000000243200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

std::vector<std::uint8_t> CorruptExtendsBytes()
{
    std::string hex(kCorruptExtendsHex);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int hi = nibble(hex[i]);
        int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            throw std::runtime_error("bad hex");
        bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return bytes;
}

// One expected base entry: the reflection name, the kind, and whether the
// GetDefinition() lookup resolves.
struct ExpectedBase {
    const char* name;
    TS::TypeKind kind;
    bool hasDefinition;
};

void ExpectBases(const TS::ITypeDefinition* td,
                 const std::vector<ExpectedBase>& expected,
                 const char* label)
{
    ASSERT_NE(td, nullptr) << label;
    std::vector<TS::ITypePtr> bases = td->DirectBaseTypes();
    ASSERT_EQ(bases.size(), expected.size()) << label;
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(bases[i]->ReflectionName(), std::string(expected[i].name))
            << label << " base " << i;
        EXPECT_EQ(bases[i]->Kind(), expected[i].kind)
            << label << " base " << i;
        if (expected[i].hasDefinition)
            EXPECT_NE(bases[i]->GetDefinition(), nullptr)
                << label << " base " << i;
        else
            EXPECT_EQ(bases[i]->GetDefinition(), nullptr)
                << label << " base " << i;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// D: DirectBaseTypes over mscorlib (the gold matrix)
// ---------------------------------------------------------------------------

TEST(ResolveTypeDirectBaseTypesTest, DirectBaseTypesMscorlibMatrix)
{
    MscorlibFixture f;

    // object: no bases at all (not an interface).
    ExpectBases(f.Type("System", "Object"), {}, "object");

    // string: the Extends Object + 7 InterfaceImpl rows in table order.
    ExpectBases(f.Type("System", "String"),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.IComparable", TS::TypeKind::Interface, true },
            { "System.ICloneable", TS::TypeKind::Interface, true },
            { "System.IConvertible", TS::TypeKind::Interface, true },
            { "System.Collections.IEnumerable", TS::TypeKind::Interface, true },
            { "System.IComparable`1[[System.String]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IEnumerable`1[[System.Char]]", TS::TypeKind::Interface, true },
            { "System.IEquatable`1[[System.String]]", TS::TypeKind::Interface, true },
        },
        "string");

    // int32: the ValueType base + 5 interfaces.
    ExpectBases(f.Type("System", "Int32"),
        {
            { "System.ValueType", TS::TypeKind::Class, true },
            { "System.IComparable", TS::TypeKind::Interface, true },
            { "System.IFormattable", TS::TypeKind::Interface, true },
            { "System.IConvertible", TS::TypeKind::Interface, true },
            { "System.IComparable`1[[System.Int32]]", TS::TypeKind::Interface, true },
            { "System.IEquatable`1[[System.Int32]]", TS::TypeKind::Interface, true },
        },
        "int32");

    // enum / valuetype / systemenum: the special base chains.
    ExpectBases(f.Type("System", "DayOfWeek"),
        { { "System.Enum", TS::TypeKind::Class, true } }, "enum");
    ExpectBases(f.Type("System", "ValueType"),
        { { "System.Object", TS::TypeKind::Class, true } }, "valuetype");
    ExpectBases(f.Type("System", "Enum"),
        {
            { "System.ValueType", TS::TypeKind::Class, true },
            { "System.IComparable", TS::TypeKind::Interface, true },
            { "System.IFormattable", TS::TypeKind::Interface, true },
            { "System.IConvertible", TS::TypeKind::Interface, true },
        },
        "systemenum");

    // iface-nobase: the interface fallback adds FindType(Object).
    ExpectBases(f.Type("System", "IComparable"),
        { { "System.Object", TS::TypeKind::Class, true } }, "iface-nobase");
    // iface-bases: Object FIRST, then the InterfaceImpl rows.
    ExpectBases(f.Type("System.Collections", "IList"),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.Collections.ICollection", TS::TypeKind::Interface, true },
            { "System.Collections.IEnumerable", TS::TypeKind::Interface, true },
        },
        "iface-bases");

    // list: the Extends Object + 9 interface rows (the generic ones
    // parameterized over the VAR).
    ExpectBases(f.Type("System.Collections.Generic", "List", 1),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.Collections.Generic.IList`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.ICollection`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IEnumerable`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.IEnumerable", TS::TypeKind::Interface, true },
            { "System.Collections.IList", TS::TypeKind::Interface, true },
            { "System.Collections.ICollection", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IReadOnlyList`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IReadOnlyCollection`1[[`0]]", TS::TypeKind::Interface, true },
        },
        "list");

    // roc-typespec: the generic collection whose interface rows are TypeSpec
    // generic instantiations.
    ExpectBases(f.Type("System.Collections.ObjectModel", "ReadOnlyCollection", 1),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.Collections.Generic.IList`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.ICollection`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IEnumerable`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.IEnumerable", TS::TypeKind::Interface, true },
            { "System.Collections.IList", TS::TypeKind::Interface, true },
            { "System.Collections.ICollection", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IReadOnlyList`1[[`0]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IReadOnlyCollection`1[[`0]]", TS::TypeKind::Interface, true },
        },
        "roc-typespec");

    // delegate / mdelegate / array / attribute / exception / nullable /
    // guid / static.
    ExpectBases(f.Type("System", "Action", 1),
        { { "System.MulticastDelegate", TS::TypeKind::Class, true } }, "delegate");
    ExpectBases(f.Type("System", "MulticastDelegate"),
        { { "System.Delegate", TS::TypeKind::Class, true } }, "mdelegate");
    ExpectBases(f.Type("System", "Array"),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.ICloneable", TS::TypeKind::Interface, true },
            { "System.Collections.IList", TS::TypeKind::Interface, true },
            { "System.Collections.ICollection", TS::TypeKind::Interface, true },
            { "System.Collections.IEnumerable", TS::TypeKind::Interface, true },
            { "System.Collections.IStructuralComparable", TS::TypeKind::Interface, true },
            { "System.Collections.IStructuralEquatable", TS::TypeKind::Interface, true },
        },
        "array");
    ExpectBases(f.Type("System", "Attribute"),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.Runtime.InteropServices._Attribute", TS::TypeKind::Interface, true },
        },
        "attribute");
    ExpectBases(f.Type("System", "Exception"),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.Runtime.Serialization.ISerializable", TS::TypeKind::Interface, true },
            { "System.Runtime.InteropServices._Exception", TS::TypeKind::Interface, true },
        },
        "exception");
    ExpectBases(f.Type("System", "Nullable", 1),
        { { "System.ValueType", TS::TypeKind::Class, true } }, "nullable");
    ExpectBases(f.Type("System", "Guid"),
        {
            { "System.ValueType", TS::TypeKind::Class, true },
            { "System.IFormattable", TS::TypeKind::Interface, true },
            { "System.IComparable", TS::TypeKind::Interface, true },
            { "System.IComparable`1[[System.Guid]]", TS::TypeKind::Interface, true },
            { "System.IEquatable`1[[System.Guid]]", TS::TypeKind::Interface, true },
        },
        "guid");
    ExpectBases(f.Type("Microsoft.Win32", "Win32Native"),
        { { "System.Object", TS::TypeKind::Class, true } }, "static");

    // nested: Dictionary`2 (its metadata-name lookup under arity 2).
    ExpectBases(f.Type("System.Collections.Generic", "Dictionary", 2),
        {
            { "System.Object", TS::TypeKind::Class, true },
            { "System.Collections.Generic.IDictionary`2[[`0],[`1]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.ICollection`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IEnumerable`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]", TS::TypeKind::Interface, true },
            { "System.Collections.IEnumerable", TS::TypeKind::Interface, true },
            { "System.Collections.IDictionary", TS::TypeKind::Interface, true },
            { "System.Collections.ICollection", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IReadOnlyDictionary`2[[`0],[`1]]", TS::TypeKind::Interface, true },
            { "System.Collections.Generic.IReadOnlyCollection`1[[System.Collections.Generic.KeyValuePair`2[[`0],[`1]]]]", TS::TypeKind::Interface, true },
            { "System.Runtime.Serialization.ISerializable", TS::TypeKind::Interface, true },
            { "System.Runtime.Serialization.IDeserializationCallback", TS::TypeKind::Interface, true },
        },
        "nested");
}

TEST(ResolveTypeDirectBaseTypesTest, DirectBaseTypesCacheAndIdentity)
{
    MscorlibFixture f;

    // Two reads return the same ELEMENTS (the LazyInit cache -- the port's
    // by-value snapshot keeps the shared_ptr identity).
    const TS::ITypeDefinition* str = f.Type("System", "String");
    std::vector<TS::ITypePtr> first = str->DirectBaseTypes();
    std::vector<TS::ITypePtr> second = str->DirectBaseTypes();
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
        EXPECT_EQ(first[i].get(), second[i].get()) << i;

    // The Object base IS the module's cached Object definition (the TypeDef
    // arm routes the entity cache).
    const TS::ITypeDefinition* obj = f.Type("System", "Object");
    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first[0]->GetDefinition(), obj);

    // The interface-fallback Object IS FindType(Object) (the KnownTypeCache
    // resolves through the module's entity cache).
    const TS::ITypeDefinition* comparable = f.Type("System", "IComparable");
    std::vector<TS::ITypePtr> ifBases = comparable->DirectBaseTypes();
    ASSERT_EQ(ifBases.size(), 1u);
    EXPECT_EQ(ifBases[0].get(),
              &f.compilation.FindType(TS::KnownTypeCode::Object));
}

TEST(ResolveTypeDirectBaseTypesTest, DirectBaseTypesSystemOnlyUnresolved)
{
    if (!FileExists(SystemPath()))
        GTEST_SKIP() << "System.dll fixture not present";

    TM::MetadataFile file{ SystemPath() };
    CacheCompilation compilation;
    TS::MetadataModule module{ compilation, &file,
                               TS::TypeSystemOptions::Default };
    compilation.SetMainModule(&module);

    // The mscorlib TypeRef the single-module compilation cannot resolve ->
    // UnknownType with the full name (gold sections D system-only).
    const TS::ITypeDefinition* powerMode =
        module.GetTypeDefinition(TS::TopLevelTypeName(
            "Microsoft.Win32", "PowerModeChangedEventArgs"));
    ASSERT_NE(powerMode, nullptr);
    ExpectBases(powerMode,
        { { "System.EventArgs", TS::TypeKind::Unknown, false } },
        "crossref-unresolved");

    // An in-module type whose Extends is still a cross-module TypeRef
    // (ProcessStartInfo : the mscorlib System.Object TypeRef).
    const TS::ITypeDefinition* psi = module.GetTypeDefinition(
        TS::TopLevelTypeName("System.Diagnostics", "ProcessStartInfo"));
    ASSERT_NE(psi, nullptr);
    ExpectBases(psi,
        { { "System.Object", TS::TypeKind::Unknown, false } },
        "inmodule");
}

TEST(ResolveTypeDirectBaseTypesTest, DirectBaseTypesCrossModuleResolved)
{
    if (!FileExists(SystemPath()) || !FileExists(MscorlibPath()))
        GTEST_SKIP() << "fixtures not present";

    CompilationBundle bundle;
    TS::MetadataModule& system = bundle.Add(SystemPath());
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    const TS::ITypeDefinition* powerMode =
        system.GetTypeDefinition(TS::TopLevelTypeName(
            "Microsoft.Win32", "PowerModeChangedEventArgs"));
    ASSERT_NE(powerMode, nullptr);
    ExpectBases(powerMode,
        { { "System.EventArgs", TS::TypeKind::Class, true } },
        "crossref-resolved");

    // The resolved base IS the mscorlib module's cached EventArgs
    // definition (the declaring-module resolution routes
    // FindModuleByReference into the referenced module).
    const TS::ITypeDefinition* eventArgs = mscorlib.GetTypeDefinition(
        TS::TopLevelTypeName("System", "EventArgs"));
    ASSERT_NE(eventArgs, nullptr);
    std::vector<TS::ITypePtr> bases = powerMode->DirectBaseTypes();
    ASSERT_EQ(bases.size(), 1u);
    EXPECT_EQ(bases[0]->GetDefinition(), eventArgs);
}

// ---------------------------------------------------------------------------
// R: ResolveType's arms (gold sections R)
// ---------------------------------------------------------------------------

TEST(ResolveTypeDirectBaseTypesTest, ResolveTypeNilAndDefinitionArms)
{
    MscorlibFixture f;
    TS::GenericContext emptyContext{ std::vector<const TS::ITypeParameter*>{} };

    // R1: nil -> the SpecialType.UnknownType null object ("?",
    // Kind::Unknown, no definition).
    TS::ITypePtr nil = f.module.ResolveType(0, emptyContext);
    EXPECT_EQ(nil->ReflectionName(), std::string("?"));
    EXPECT_EQ(nil->Kind(), TS::TypeKind::Unknown);
    EXPECT_EQ(nil->GetDefinition(), nullptr);

    // R2: the TypeDefinition arm -> the entity-cache identity (String is
    // 0x02000073 in mscorlib 4.8).
    const TS::ITypeDefinition* stringDef = f.Type("System", "String");
    TS::ITypePtr resolved = f.module.ResolveType(0x02000073u, emptyContext);
    EXPECT_EQ(resolved.get(), stringDef);
    EXPECT_EQ(resolved->ReflectionName(), std::string("System.String"));
    EXPECT_EQ(resolved->Kind(), TS::TypeKind::Class);

    // The attrs overload (the DirectBaseTypes shape): String's own attribute
    // rows are not [Dynamic]/[TupleElementNames]/[Nullable], so the
    // ApplyAttributeTypeVisitor wrap is identity.
    TS::ITypePtr resolved2 = f.module.ResolveType(
        0x02000073u, emptyContext,
        std::optional<std::vector<std::uint32_t>>(
            f.file.GetCustomAttributeTokens(0x02000073u)));
    EXPECT_EQ(resolved2.get(), stringDef);
}

TEST(ResolveTypeDirectBaseTypesTest, ResolveTypeSpecificationArm)
{
    MscorlibFixture f;

    // R3: FileSystemEnumerableIterator`1 (0x0200018f) whose Extends is the
    // TypeSpec 0x1B00004E (the GENERICINST Iterator`1<!0>): the decode runs
    // over the type's own generic context.
    const TS::ITypeDefinition* fse = f.Type("System.IO",
                                            "FileSystemEnumerableIterator", 1);
    ASSERT_EQ(fse->MetadataToken(), 0x0200018Fu);
    TS::GenericContext context{ fse->TypeParameters() };
    TS::ITypePtr base = f.module.ResolveType(0x1B00004Eu, context);
    EXPECT_EQ(base->ReflectionName(),
              std::string("System.IO.Iterator`1[[`0]]"));
    EXPECT_EQ(base->Kind(), TS::TypeKind::Class);
    // The structure: a ParameterizedType whose generic type is the Iterator`1
    // definition and whose argument is the type parameter (ReflectionName
    // "`0", Name "T").
    auto* pt = dynamic_cast<const TS::ParameterizedType*>(base.get());
    ASSERT_NE(pt, nullptr);
    EXPECT_EQ(pt->GenericType()->ReflectionName(),
              std::string("System.IO.Iterator`1"));
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    EXPECT_EQ(pt->TypeArguments()[0]->ReflectionName(), std::string("`0"));
    // The authored type-parameter name (the gold's ReflectionName "`0" is
    // index-derived; the Name is the authored identifier).
    EXPECT_EQ(pt->TypeArguments()[0]->Name(), std::string("TSource"));

    // The DirectBaseTypes read resolves the same base (the TypeSpec is the
    // ONLY base -- no InterfaceImpl rows).
    std::vector<TS::ITypePtr> bases = fse->DirectBaseTypes();
    ASSERT_EQ(bases.size(), 1u);
    EXPECT_EQ(bases[0]->ReflectionName(),
              std::string("System.IO.Iterator`1[[`0]]"));
}

TEST(ResolveTypeDirectBaseTypesTest, ResolveTypeReferenceArms)
{
    if (!FileExists(SystemPath()))
        GTEST_SKIP() << "System.dll fixture not present";
    TS::GenericContext emptyContext{ std::vector<const TS::ITypeParameter*>{} };

    // R4 (resolved): over System.dll main + mscorlib, the PowerModeChanged
    // base TypeRef (0x01000014) resolves into the mscorlib module.
    {
        CompilationBundle bundle;
        TS::MetadataModule& system = bundle.Add(SystemPath());
        TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
        bundle.Seal();
        TS::ITypePtr base = system.ResolveType(0x01000014u, emptyContext);
        EXPECT_EQ(base->ReflectionName(), std::string("System.EventArgs"));
        EXPECT_EQ(base->Kind(), TS::TypeKind::Class);
        const TS::ITypeDefinition* eventArgs = mscorlib.GetTypeDefinition(
            TS::TopLevelTypeName("System", "EventArgs"));
        ASSERT_NE(eventArgs, nullptr);
        EXPECT_EQ(base.get(), eventArgs);
    }

    // R4 (miss): over System.dll alone, the same TypeRef falls to the
    // UnknownType with the full name.
    {
        TM::MetadataFile file{ SystemPath() };
        CacheCompilation compilation;
        TS::MetadataModule module{ compilation, &file,
                                   TS::TypeSystemOptions::Default };
        compilation.SetMainModule(&module);
        TS::ITypePtr base = module.ResolveType(0x01000014u, emptyContext);
        EXPECT_EQ(base->ReflectionName(), std::string("System.EventArgs"));
        EXPECT_EQ(base->Kind(), TS::TypeKind::Unknown);
        EXPECT_EQ(base->GetDefinition(), nullptr);
    }
}

#if defined(_WIN32)
TEST(ResolveTypeDirectBaseTypesTest, ResolveTypeExportedTypeArm)
{
    if (!FileExists(FacadePath()))
        GTEST_SKIP() << "System.Runtime facade not present";
    TS::GenericContext emptyContext{ std::vector<const TS::ITypeParameter*>{} };

    // R5: the facade's System.String forwarder (ExportedType 0x270000ED)
    // resolves through ResolveForwardedType into the mscorlib module's
    // String definition.
    CompilationBundle bundle;
    TS::MetadataModule& facade = bundle.Add(FacadePath());
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();
    TS::ITypePtr fwd = facade.ResolveType(0x270000EDu, emptyContext);
    EXPECT_EQ(fwd->ReflectionName(), std::string("System.String"));
    EXPECT_EQ(fwd->Kind(), TS::TypeKind::Class);
    const TS::ITypeDefinition* str = mscorlib.GetTypeDefinition(
        TS::TopLevelTypeName("System", "String"));
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(fwd.get(), str);
}
#endif

TEST(ResolveTypeDirectBaseTypesTest, ResolveTypeDefaultAndOutOfRangeArms)
{
    MscorlibFixture f;
    TS::GenericContext emptyContext{ std::vector<const TS::ITypeParameter*>{} };

    // R6: a MethodDef token -> the BadImageFormatException default arm.
    try
    {
        f.module.ResolveType(0x06000001u, emptyContext);
        FAIL() << "expected std::invalid_argument";
    }
    catch (const std::invalid_argument& ex)
    {
        EXPECT_STREQ(ex.what(), "Not a type handle");
    }

    // R7: a TypeDef token past the table -> HandleOutOfRange (what the
    // corrupt-Extends catch arm swallows).
    try
    {
        f.module.ResolveType(0x0200FFFFu, emptyContext);
        FAIL() << "expected std::out_of_range";
    }
    catch (const std::out_of_range& ex)
    {
        EXPECT_STREQ(ex.what(), "Handle with invalid row number.");
    }
}

// ---------------------------------------------------------------------------
// X: the corrupt-Extends catch arm (gold section X)
// ---------------------------------------------------------------------------

TEST(ResolveTypeDirectBaseTypesTest, CorruptExtendsCaughtIntoUnknownType)
{
    std::vector<std::uint8_t> bytes = CorruptExtendsBytes();
    ASSERT_EQ(bytes.size(), 2048u);

    // The manifest goes through a temp file (MetadataFile takes a path); the
    // deterministic relative name matches the XmlSerialization_Test
    // convention (the guard removes it at scope exit).
    std::string path = "resolve_type_corrupt_extends.tmp";
    {
        FILE* out = std::fopen(path.c_str(), "wb");
        ASSERT_NE(out, nullptr) << path;
        ASSERT_EQ(std::fwrite(bytes.data(), 1, bytes.size(), out), bytes.size());
        std::fclose(out);
    }
    struct PathGuard {
        std::string p;
        ~PathGuard() { std::remove(p.c_str()); }
    } guard{ path };

    TM::MetadataFile manifest{ path };
    ASSERT_TRUE(manifest.IsValid()) << "the patched manifest must parse";
    CacheCompilation compilation;
    TS::MetadataModule module{ compilation, &manifest,
                               TS::TypeSystemOptions::Default };
    compilation.SetMainModule(&module);

    const TS::ITypeDefinition* bad = module.GetTypeDefinition(
        TS::TopLevelTypeName("Rt", "Bad"));
    ASSERT_NE(bad, nullptr);
    // The catch arm: the out-of-range Extends resolution is swallowed into
    // the SpecialType.UnknownType null object (the gold's "?" render).
    std::vector<TS::ITypePtr> bases = bad->DirectBaseTypes();
    ASSERT_EQ(bases.size(), 1u);
    EXPECT_EQ(bases[0]->ReflectionName(), std::string("?"));
    EXPECT_EQ(bases[0]->Kind(), TS::TypeKind::Unknown);
    EXPECT_EQ(bases[0]->GetDefinition(), nullptr);

    // The sibling row is untouched: a nil Extends class has no bases.
    const TS::ITypeDefinition* plain = module.GetTypeDefinition(
        TS::TopLevelTypeName("Rt", "Plain"));
    ASSERT_NE(plain, nullptr);
    ExpectBases(plain, {}, "plain");
}
