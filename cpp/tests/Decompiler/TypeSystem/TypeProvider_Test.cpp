// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
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

// Tests for the TypeSystem TypeProvider + GenericContext port
// (ICSharpCode.Decompiler/TypeSystem/TypeProvider.cs + GenericContext.cs +
// Implementation/PinnedType.cs + FunctionPointerType.FromSignature): every
// expectation is pinned against the real ICSharpCode.Decompiler 11.0 engine
// driven over the SAME fixture shape (a SimpleCompilation of the real
// mscorlib 4.8 + System.dll 4.8) by the C:/temp-probe/TypeProviderProbe gold
// probe (gold_final.txt):
//   * the primitive matrix (the 18 signature primitive codes -> FindType);
//   * the composite arms (SZArray/Array/Ptr/ByRef/Pinned/ModReq/ModOpt/
//     GenericInst, the arity-mismatch rules);
//   * the GenericContext scopes (the four ctors, the Dummy fallbacks,
//     ToSubstitution);
//   * real-blob end-to-end decodes through the walker (List`1.Add(T),
//     List`1.CopyTo(T[]) + List`1._items(T[]), String.CopyTo's char[]);
//   * the crafted FnPtr/CallConv/modreq matrix (the FromSignature calling-
//     convention table, the In/Out marker arms, the unmatched-modifier
//     keep, the multi-dim array, the local-signature pinned element);
//   * the resolution arms (GetTypeFromDefinition over real TypeDefs, the
//     List`1 IList`1 InterfaceImpl TypeSpec, System.dll TypeRef resolution:
//     resolvable / unresolvable / nested-declaring-scope);
//   * the attribute-provider arms (GetSystemType, IsSystemType,
//     GetTypeFromSerializedName, GetUnderlyingEnumType incl. the
//     EnumUnderlyingTypeResolveException);
//   * the compilation-only provider's documented divergence (the port's
//     contract derives the reader from the module, so the reader-dependent
//     arms throw instead of building the UnknownType fallback the C# builds
//     through the caller's reader -- the gold documents the C# behavior).
//
// NOTE on the name renders: the gold's reflection names for
// ParameterizedType use the C# `[[arg]]` form; the port's minimal
// ParameterizedType renders `<arg>` (a pre-existing minimal-port divergence
// pinned by the Phase-3/4 signature-decode tests), so the generic arms pin
// the STRUCTURE (kind/tpc/generic/arg identity) rather than the reflection
// render. Array/Ptr/ByRef/Pinned/ModReq/ModOpt reflection renders DO match
// and are pinned directly.

#include <gtest/gtest.h>
#include <cstdlib>

#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "tests/Decompiler/TypeSystem/LookupStubs.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;
namespace TestSupport = ILSpy::Decompiler::TypeSystem::TestSupport;

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

// A module reference resolving to an externally-owned module (the C#
// `PEFile : IModuleReference` shape the gold probe's
// `new SimpleCompilation(new PEFile(...), new PEFile(...))` drives).
class FixedModuleRef : public TS::IModuleReference {
public:
    explicit FixedModuleRef(const TS::IModule* module = nullptr)
        : module_(module) {}

    const TS::IModule* Resolve(
        const TS::ITypeResolveContext&) const override {
        return module_;
    }

private:
    const TS::IModule* module_;
};

// The port's SimpleCompilation with the protected Init exposed (the
// SimpleCompilationTest TestSimpleCompilation pattern).
class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

// The gold fixture shape: a SimpleCompilation of the real mscorlib (main) +
// System.dll (referenced), with a real MetadataModule over each file. The
// modules are constructed BEFORE the compilation's Init (the ctor only
// stores the compilation reference), mirroring the probe's
// `new SimpleCompilation(new PEFile(...), new PEFile(...))`. FindType
// resolves through the real KnownTypeCache -> SearchType -> the modules'
// GetTypeDefinition, so the primitive arms resolve the REAL mscorlib
// definitions exactly as the gold's FindType does.
struct MscorlibSystemFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TM::MetadataFile systemFile{ SystemPath() };
    TestCompilation compilation;
    std::unique_ptr<TS::MetadataModule> mscorlibModule;
    std::unique_ptr<TS::MetadataModule> systemModule;
    FixedModuleRef mscorlibRef;
    FixedModuleRef systemRef;

    MscorlibSystemFixture() {
        mscorlibModule = std::make_unique<TS::MetadataModule>(
            compilation, &mscorlibFile, TS::TypeSystemOptions::Default);
        systemModule = std::make_unique<TS::MetadataModule>(
            compilation, &systemFile, TS::TypeSystemOptions::Default);
        mscorlibRef = FixedModuleRef(mscorlibModule.get());
        systemRef = FixedModuleRef(systemModule.get());
        compilation.Initialize(mscorlibRef, { &systemRef });
    }

    // The C# `internal readonly TypeProvider TypeProvider` field's accessor
    // (the MetadataModule ctor's `new TypeProvider(this)`). NON-CONST: the
    // port's ISignatureTypeProvider contract mirrors the C#'s non-const
    // methods (the C# has no const), so the walker-driven arms need a
    // mutable provider handle -- the const_cast is the probe-equivalent
    // access (no state is mutated).
    TS::TypeProvider& Provider() const {
        return const_cast<TS::TypeProvider&>(mscorlibModule->TypeProvider());
    }

    // A TypeDef token by (namespace, metadata name) -- the fixture's files
    // carry the same rows the gold probe scanned (mscorlib 4.8: String
    // 0x02000073, Int32 0x020000FB, Object 0x0200003D, List`1 0x020004DC).
    // The `` N`` arity suffix of the metadata name is split off (the reverse
    // lookup keys on the plain name + the type-parameter count).
    std::uint32_t TypeDefToken(const TM::MetadataFile& file,
        const char* ns, const char* name) {
        std::string n(name);
        int tpc = 0;
        auto pos = n.rfind('`');
        if (pos != std::string::npos) {
            tpc = std::stoi(n.substr(pos + 1));
            n = n.substr(0, pos);
        }
        std::uint32_t token = file.GetTypeDefinition(
            TS::TopLevelTypeName(ns, n, tpc));
        if (token == 0) throw std::runtime_error(std::string(ns) + "." + name);
        return token;
    }
};

// The coded-index splice helper (the gold probe's C()).
void Append(std::vector<std::uint8_t>& blob,
            const std::vector<std::uint8_t>& tail) {
    blob.insert(blob.end(), tail.begin(), tail.end());
}

// The TypeDefOrRefEncoded coded index for a TypeDef token: (row << 2) | 0,
// then the compressed unsigned form (the gold probe's CodedDef + C).
std::vector<std::uint8_t> CodedForToken(std::uint32_t token) {
    std::uint32_t coded = (token & 0xFFFFFF) << 2;
    if (coded < 0x80) return { static_cast<std::uint8_t>(coded) };
    if (coded < 0x4000)
        return { static_cast<std::uint8_t>(0x80 | (coded >> 8)),
                 static_cast<std::uint8_t>(coded & 0xFF) };
    return { static_cast<std::uint8_t>(0xC0 | (coded >> 24)),
             static_cast<std::uint8_t>((coded >> 16) & 0xFF),
             static_cast<std::uint8_t>((coded >> 8) & 0xFF),
             static_cast<std::uint8_t>(coded & 0xFF) };
}

std::string IsRefStr(const TS::IType& t) {
    auto r = t.IsReferenceType();
    if (!r) return "null";
    return *r ? "true" : "false";
}

} // namespace

// ---------------------------------------------------------------------------
// A: the primitive matrix (gold section A). The real engine's FindType
// resolves the REAL mscorlib definitions (Int32 token 0x020000FB, String
// 0x02000073); the port's KnownTypeCache->SearchType walks the compilation's
// modules' GetTypeDefinition -- the same resolution.
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, PrimitiveMatrixMatchesGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();

    struct Row { TM::PrimitiveTypeCode code; TS::TypeKind kind;
        const char* name; const char* reflection; bool isRef;
        TS::KnownTypeCode ktc; };
    const Row rows[] = {
        { TM::PrimitiveTypeCode::Void, TS::TypeKind::Void, "Void",
          "System.Void", false, TS::KnownTypeCode::Void },
        { TM::PrimitiveTypeCode::Boolean, TS::TypeKind::Struct, "Boolean",
          "System.Boolean", false, TS::KnownTypeCode::Boolean },
        { TM::PrimitiveTypeCode::Char, TS::TypeKind::Struct, "Char",
          "System.Char", false, TS::KnownTypeCode::Char },
        { TM::PrimitiveTypeCode::SByte, TS::TypeKind::Struct, "SByte",
          "System.SByte", false, TS::KnownTypeCode::SByte },
        { TM::PrimitiveTypeCode::Byte, TS::TypeKind::Struct, "Byte",
          "System.Byte", false, TS::KnownTypeCode::Byte },
        { TM::PrimitiveTypeCode::Int16, TS::TypeKind::Struct, "Int16",
          "System.Int16", false, TS::KnownTypeCode::Int16 },
        { TM::PrimitiveTypeCode::UInt16, TS::TypeKind::Struct, "UInt16",
          "System.UInt16", false, TS::KnownTypeCode::UInt16 },
        { TM::PrimitiveTypeCode::Int32, TS::TypeKind::Struct, "Int32",
          "System.Int32", false, TS::KnownTypeCode::Int32 },
        { TM::PrimitiveTypeCode::UInt32, TS::TypeKind::Struct, "UInt32",
          "System.UInt32", false, TS::KnownTypeCode::UInt32 },
        { TM::PrimitiveTypeCode::Int64, TS::TypeKind::Struct, "Int64",
          "System.Int64", false, TS::KnownTypeCode::Int64 },
        { TM::PrimitiveTypeCode::UInt64, TS::TypeKind::Struct, "UInt64",
          "System.UInt64", false, TS::KnownTypeCode::UInt64 },
        { TM::PrimitiveTypeCode::Single, TS::TypeKind::Struct, "Single",
          "System.Single", false, TS::KnownTypeCode::Single },
        { TM::PrimitiveTypeCode::Double, TS::TypeKind::Struct, "Double",
          "System.Double", false, TS::KnownTypeCode::Double },
        { TM::PrimitiveTypeCode::String, TS::TypeKind::Class, "String",
          "System.String", true, TS::KnownTypeCode::String },
        { TM::PrimitiveTypeCode::TypedReference, TS::TypeKind::Struct,
          "TypedReference", "System.TypedReference", false,
          TS::KnownTypeCode::TypedReference },
        { TM::PrimitiveTypeCode::IntPtr, TS::TypeKind::Struct, "IntPtr",
          "System.IntPtr", false, TS::KnownTypeCode::IntPtr },
        { TM::PrimitiveTypeCode::UIntPtr, TS::TypeKind::Struct, "UIntPtr",
          "System.UIntPtr", false, TS::KnownTypeCode::UIntPtr },
        { TM::PrimitiveTypeCode::Object, TS::TypeKind::Class, "Object",
          "System.Object", true, TS::KnownTypeCode::Object },
    };
    for (const auto& r : rows) {
        TS::ITypePtr t = provider.GetPrimitiveType(r.code);
        ASSERT_NE(t, nullptr) << r.name;
        EXPECT_EQ(t->Kind(), r.kind) << r.name;
        EXPECT_EQ(t->Name(), std::string(r.name)) << r.name;
        EXPECT_EQ(t->ReflectionName(), std::string(r.reflection)) << r.name;
        EXPECT_EQ(t->TypeParameterCount(), 0) << r.name;
        EXPECT_EQ(IsRefStr(*t), r.isRef ? "true" : "false") << r.name;
        // The KnownTypeCode of the resolved definition (the gold's ktc).
        const TS::ITypeDefinition* def = t->GetDefinition();
        ASSERT_NE(def, nullptr) << r.name;
        EXPECT_EQ(def->KnownTypeCode(), r.ktc) << r.name;
    }

    // Two spot tokens (the gold's `[elem] token=` lines): the FindType
    // results are the REAL mscorlib definitions.
    TS::ITypePtr int32 = provider.GetPrimitiveType(
        TM::PrimitiveTypeCode::Int32);
    EXPECT_EQ(int32->GetDefinition()->MetadataToken(), 0x020000FBu);
    TS::ITypePtr str = provider.GetPrimitiveType(
        TM::PrimitiveTypeCode::String);
    EXPECT_EQ(str->GetDefinition()->MetadataToken(), 0x02000073u);
}

// ---------------------------------------------------------------------------
// B: the composite arms (gold section B). The reflection-name renders match
// the C# for Array/Ptr/ByRef/Pinned/ModReq/ModOpt; the generic arms pin the
// STRUCTURE (the ParameterizedType render divergence is a minimal-port
// artifact -- see the file note).
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, CompositeArmsMatchGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();
    TS::ITypePtr int32 = provider.GetPrimitiveType(
        TM::PrimitiveTypeCode::Int32);
    TS::ITypePtr str = provider.GetPrimitiveType(
        TM::PrimitiveTypeCode::String);

    // B1: SZArray over int -- dims 1, reflection "System.Int32[]" (an array
    // is a reference type).
    {
        TS::ITypePtr t = provider.GetSZArrayType(int32);
        auto* a = dynamic_cast<const TS::ArrayType*>(t.get());
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->Kind(), TS::TypeKind::Array);
        EXPECT_EQ(a->ReflectionName(), "System.Int32[]");
        EXPECT_TRUE(a->IsSzArray());
        EXPECT_EQ(a->Rank(), 1);
        EXPECT_EQ(IsRefStr(*a), "true");
        EXPECT_EQ(a->Element()->GetDefinition()->MetadataToken(), 0x020000FBu);
    }
    // B2: multi-dim rank 2 -- reflection "System.Int32[,]".
    {
        TM::ArrayShape shape;
        shape.Rank = 2;
        TS::ITypePtr t = provider.GetArrayType(int32, shape);
        auto* a = dynamic_cast<const TS::ArrayType*>(t.get());
        ASSERT_NE(a, nullptr);
        EXPECT_FALSE(a->IsSzArray());
        EXPECT_EQ(a->Rank(), 2);
        EXPECT_EQ(a->ReflectionName(), "System.Int32[,]");
    }
    // B3: multi-dim rank 3 with 2 sizes and 3 lower bounds.
    {
        TM::ArrayShape shape;
        shape.Rank = 3;
        shape.Sizes = { 5, 6 };
        shape.LowerBounds = { -1, 0, 2 };
        TS::ITypePtr t = provider.GetArrayType(int32, shape);
        auto* a = dynamic_cast<const TS::ArrayType*>(t.get());
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->Rank(), 3);
        EXPECT_EQ(a->ReflectionName(), "System.Int32[,,]");
    }
    // B4: pointer -- reference-ness unknown.
    {
        TS::ITypePtr t = provider.GetPointerType(int32);
        auto* p = dynamic_cast<const TS::PointerType*>(t.get());
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->ReflectionName(), "System.Int32*");
        EXPECT_EQ(IsRefStr(*p), "null");
    }
    // B5: byref.
    {
        TS::ITypePtr t = provider.GetByReferenceType(int32);
        auto* b = dynamic_cast<const TS::ByReferenceType*>(t.get());
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(b->ReflectionName(), "System.Int32&");
        EXPECT_TRUE(b->IsByRefLike());
        EXPECT_EQ(IsRefStr(*b), "null");
    }
    // B6: pinned -- Kind Other, the " pinned" suffix renders on BOTH Name and
    // ReflectionName, reference-ness delegates to the element (false).
    {
        TS::ITypePtr t = provider.GetPinnedType(int32);
        auto* p = dynamic_cast<const TS::PinnedType*>(t.get());
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->Kind(), TS::TypeKind::Other);
        EXPECT_EQ(p->Name(), "Int32 pinned");
        EXPECT_EQ(p->ReflectionName(), "System.Int32 pinned");
        EXPECT_EQ(IsRefStr(*p), "false");
        EXPECT_EQ(p->Element()->GetDefinition()->MetadataToken(), 0x020000FBu);
    }
    // B7/B8: modified (required/optional) -- the " modreq(...)" render.
    {
        TS::ITypePtr t = provider.GetModifiedType(str, int32, true);
        auto* m = dynamic_cast<const TS::ModifiedType*>(t.get());
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->Kind(), TS::TypeKind::ModReq);
        EXPECT_TRUE(m->IsRequired());
        EXPECT_EQ(m->Modifier()->GetDefinition()->MetadataToken(), 0x02000073u);
        EXPECT_EQ(m->Element()->GetDefinition()->MetadataToken(), 0x020000FBu);
        EXPECT_EQ(m->ReflectionName(), "System.Int32 modreq(System.String)");
        EXPECT_EQ(IsRefStr(*m), "false");  // delegates to the element

        TS::ITypePtr t2 = provider.GetModifiedType(str, int32, false);
        auto* m2 = dynamic_cast<const TS::ModifiedType*>(t2.get());
        ASSERT_NE(m2, nullptr);
        EXPECT_EQ(m2->Kind(), TS::TypeKind::ModOpt);
        EXPECT_FALSE(m2->IsRequired());
        EXPECT_EQ(m2->ReflectionName(), "System.Int32 modopt(System.String)");
    }
    // B9-B12: the generic instantiation arms.
    {
        std::uint32_t listToken = fx.TypeDefToken(
            fx.mscorlibFile, "System.Collections.Generic", "List`1");
        TS::ITypePtr listDef = provider.GetTypeFromDefinition(
            listToken, 0x12);
        ASSERT_NE(listDef, nullptr);
        EXPECT_EQ(listDef->Kind(), TS::TypeKind::Class);
        EXPECT_EQ(listDef->ReflectionName(), "System.Collections.Generic.List`1");
        EXPECT_EQ(listDef->TypeParameterCount(), 1);
        EXPECT_EQ(listDef->GetDefinition()->MetadataToken(), 0x020004DCu);

        // List<int> parameterization (gold B10).
        TS::ITypePtr inst = provider.GetGenericInstantiation(
            listDef, { int32 });
        auto* pt = dynamic_cast<const TS::ParameterizedType*>(inst.get());
        ASSERT_NE(pt, nullptr);
        EXPECT_EQ(pt->Kind(), TS::TypeKind::Class);
        EXPECT_EQ(pt->TypeParameterCount(), 1);
        EXPECT_EQ(pt->GenericType().get(), listDef.get());
        ASSERT_EQ(pt->TypeArguments().size(), 1u);
        EXPECT_EQ(pt->TypeArguments()[0]->GetDefinition()->MetadataToken(),
                  0x020000FBu);

        // The arity-mismatch arm: 2 args on a tpc-1 generic returns the
        // generic UNCHANGED (gold B11).
        TS::ITypePtr mismatch = provider.GetGenericInstantiation(
            listDef, { int32, str });
        EXPECT_EQ(mismatch.get(), listDef.get());

        // The non-generic arm (gold B12): Int32 with any args returns Int32.
        TS::ITypePtr nogen = provider.GetGenericInstantiation(int32, { str });
        EXPECT_EQ(nogen.get(), int32.get());
    }
}

// ---------------------------------------------------------------------------
// C: the GenericContext scopes (gold section C). The class-parameter arms
// run over the REAL List`1 type parameters (the port's
// MetadataTypeParameter: Name "T", ReflectionName "`0"); the out-of-range
// arms hit the DummyTypeParameter cache ("!N" / "!!N", "`N" / "``N"). The
// method-parameter arms over a real generic method need the member family
// (a later slice: the port's MetadataModule has no GetDefinition(MethodDef)),
// so the port drives the method scope through a LookupMethod stub carrying a
// LookupTypeParameter -- the CTOR wiring is what those arms pin.
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, GenericContextScopesMatchGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();
    std::uint32_t listToken = fx.TypeDefToken(
        fx.mscorlibFile, "System.Collections.Generic", "List`1");
    const TS::ITypeDefinition* listDef = fx.mscorlibModule->GetDefinition(
        listToken);
    ASSERT_NE(listDef, nullptr);
    std::vector<const TS::ITypeParameter*> listParams =
        listDef->TypeParameters();
    ASSERT_EQ(listParams.size(), 1u);
    EXPECT_EQ(listParams[0]->Name(), "T");
    EXPECT_EQ(listParams[0]->ReflectionName(), "`0");
    EXPECT_EQ(listParams[0]->Index(), 0);

    // C1: in-range class parameter -- the parameter itself; out-of-range --
    // the cached dummy ("!5").
    TS::GenericContext gc(listParams);
    auto class0 = gc.GetClassTypeParameter(0);
    ASSERT_NE(class0, nullptr);
    EXPECT_EQ(class0->Name(), "T");
    EXPECT_EQ(class0->ReflectionName(), "`0");
    auto class5 = gc.GetClassTypeParameter(5);
    EXPECT_EQ(class5->Name(), "!5");
    EXPECT_EQ(class5->ReflectionName(), "`5");
    EXPECT_EQ(class5->Kind(), TS::TypeKind::TypeParameter);

    // C2: the class-only context's method arm -> the method dummy ("!!0").
    auto method0 = gc.GetMethodTypeParameter(0);
    EXPECT_EQ(method0->Name(), "!!0");
    EXPECT_EQ(method0->ReflectionName(), "``0");

    // The provider arms route through the context (gold C4): in-range class
    // parameter, out-of-range dummies ("!9" / "`9", "!!7" / "``7").
    TS::ITypePtr viaProvider = provider.GetGenericTypeParameter(gc, 0);
    EXPECT_EQ(viaProvider->Name(), "T");
    TS::ITypePtr class9 = provider.GetGenericTypeParameter(gc, 9);
    EXPECT_EQ(class9->Name(), "!9");
    EXPECT_EQ(class9->ReflectionName(), "`9");
    TS::ITypePtr method7 = provider.GetGenericMethodParameter(gc, 7);
    EXPECT_EQ(method7->Name(), "!!7");
    EXPECT_EQ(method7->ReflectionName(), "``7");

    // C3/C7: the class+method ctor over a method-shaped entity (the stub --
    // see the suite note).
    TestSupport::LookupTypeParameter methodParam("TM");
    TestSupport::LookupMethod method("Empty", fx.compilation);
    method.SetTypeParameters({ &methodParam });
    TS::GenericContext gc2(listParams, { &methodParam });
    EXPECT_EQ(gc2.GetClassTypeParameter(0)->Name(), "T");
    EXPECT_EQ(gc2.GetMethodTypeParameter(0)->Name(), "TM");
    EXPECT_EQ(gc2.GetMethodTypeParameter(5)->Name(), "!!5");

    // C5: ToSubstitution -- both lists present as IType aliases.
    TS::TypeParameterSubstitution subst = gc2.ToSubstitution();
    ASSERT_TRUE(subst.ClassTypeArguments().has_value());
    ASSERT_EQ(subst.ClassTypeArguments()->size(), 1u);
    EXPECT_EQ((*subst.ClassTypeArguments())[0]->Name(), "T");
    ASSERT_TRUE(subst.MethodTypeArguments().has_value());
    ASSERT_EQ(subst.MethodTypeArguments()->size(), 1u);
    EXPECT_EQ((*subst.MethodTypeArguments())[0]->Name(), "TM");

    // C6: the empty context collapses both to the null substitution.
    TS::GenericContext empty(std::vector<const TS::ITypeParameter*>{});
    TS::TypeParameterSubstitution substEmpty = empty.ToSubstitution();
    EXPECT_FALSE(substEmpty.ClassTypeArguments().has_value());
    EXPECT_FALSE(substEmpty.MethodTypeArguments().has_value());

    // C7: the IEntity ctors. A type definition entity scopes its own
    // parameters and no method parameters; a method entity scopes the
    // declaring type's parameters plus its own.
    TS::GenericContext fromTd(*listDef);
    EXPECT_EQ(fromTd.ClassTypeParameters().size(), 1u);
    EXPECT_EQ(fromTd.GetClassTypeParameter(0)->Name(), "T");
    EXPECT_EQ(fromTd.GetMethodTypeParameter(0)->Name(), "!!0");
    TS::GenericContext fromMethod(method);
    EXPECT_EQ(fromMethod.ClassTypeParameters().size(), 0u);
    EXPECT_EQ(fromMethod.GetMethodTypeParameter(0)->Name(), "TM");

    // C8: the ITypeResolveContext ctor (the SimpleTypeResolveContext over
    // the entity).
    TS::SimpleTypeResolveContext tdCtx(*listDef);
    TS::GenericContext fromCtx(tdCtx);
    EXPECT_EQ(fromCtx.ClassTypeParameters().size(), 1u);
    EXPECT_EQ(fromCtx.GetClassTypeParameter(0)->Name(), "T");
    TS::SimpleTypeResolveContext methodCtx(method);
    TS::GenericContext fromCtxM(methodCtx);
    EXPECT_EQ(fromCtxM.ClassTypeParameters().size(), 0u);
    EXPECT_EQ(fromCtxM.GetMethodTypeParameter(0)->Name(), "TM");
}

// ---------------------------------------------------------------------------
// D: real-blob end-to-end decodes (gold section D) through the port's walker
// over the port's provider: List`1.Add(T), List`1.CopyTo(T[]) (the first
// overload), the List`1._items field (T[]), and String.CopyTo's char[]
// parameter -- the VAR resolution and the SZArray composites, exactly as the
// real SRM SignatureDecoder drives them.
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, RealBlobDecodesMatchGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();
    std::uint32_t listToken = fx.TypeDefToken(
        fx.mscorlibFile, "System.Collections.Generic", "List`1");
    const TS::ITypeDefinition* listDef = fx.mscorlibModule->GetDefinition(
        listToken);
    TS::GenericContext gc(listDef->TypeParameters());
    TM::SignatureTypeProviderDecoder<TS::TypeProvider> decoder(
        provider, fx.mscorlibFile);

    auto decodeMethod = [&](std::uint32_t methodToken)
        -> TM::ProviderMethodSignature<TS::ITypePtr> {
        auto blob = fx.mscorlibFile.GetSignatureBlob(methodToken);
        EXPECT_TRUE(blob.has_value());
        return decoder.DecodeMethodSignature(blob->data(), blob->size(), gc);
    };

    std::uint32_t addToken = 0, copyToToken = 0;
    for (const auto& m : fx.mscorlibFile.GetMethods(listToken)) {
        if (m.Name == "Add") addToken = m.Token;
        if (m.Name == "CopyTo" && copyToToken == 0) copyToToken = m.Token;
    }
    ASSERT_NE(addToken, 0u);
    ASSERT_NE(copyToToken, 0u);

    // List`1.Add(T item): an instance method, void return, one T parameter.
    {
        auto sig = decodeMethod(addToken);
        EXPECT_TRUE(sig.Header.HasThis);
        EXPECT_EQ(sig.RequiredParameterCount, 1u);
        EXPECT_EQ(sig.ReturnType->ReflectionName(), "System.Void");
        ASSERT_EQ(sig.ParameterTypes.size(), 1u);
        // The VAR T resolves through the context to the REAL type parameter.
        auto* tp = dynamic_cast<const TS::ITypeParameter*>(
            sig.ParameterTypes[0].get());
        ASSERT_NE(tp, nullptr);
        EXPECT_EQ(tp->Name(), "T");
        EXPECT_EQ(tp->ReflectionName(), "`0");
        EXPECT_EQ(tp->Index(), 0);
    }
    // List`1.CopyTo(T[] array): the SZArray composite over the VAR.
    {
        auto sig = decodeMethod(copyToToken);
        ASSERT_EQ(sig.ParameterTypes.size(), 1u);
        auto* a = dynamic_cast<const TS::ArrayType*>(
            sig.ParameterTypes[0].get());
        ASSERT_NE(a, nullptr);
        EXPECT_TRUE(a->IsSzArray());
        EXPECT_EQ(a->Rank(), 1);
        EXPECT_EQ(a->ReflectionName(), "`0[]");
        auto* elem = dynamic_cast<const TS::ITypeParameter*>(
            a->Element().get());
        ASSERT_NE(elem, nullptr);
        EXPECT_EQ(elem->Name(), "T");
    }

    // The field: List`1._items (T[]) -- the field signature decodes the same
    // element composite (the 0x06 header byte is skipped; the walker's
    // DecodeType takes the bare type).
    std::uint32_t itemsToken = 0;
    for (const auto& f : fx.mscorlibFile.GetFields(listToken)) {
        if (f.Name == "_items") itemsToken = f.Token;
    }
    ASSERT_NE(itemsToken, 0u);
    auto itemsBlob = fx.mscorlibFile.GetSignatureBlob(itemsToken);
    ASSERT_TRUE(itemsBlob.has_value());
    ASSERT_GT(itemsBlob->size(), 1u);
    EXPECT_EQ((*itemsBlob)[0], 0x06);  // the FIELD signature header
    TS::ITypePtr field = decoder.DecodeType(
        itemsBlob->data() + 1, itemsBlob->size() - 1, gc);
    auto* a = dynamic_cast<const TS::ArrayType*>(field.get());
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->ReflectionName(), "`0[]");
    auto* elem = dynamic_cast<const TS::ITypeParameter*>(
        a->Element().get());
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->Name(), "T");
    EXPECT_EQ(elem->Index(), 0);

    // String.CopyTo(int, char[], int, int): the primitive FindType params +
    // the SZArray over the REAL Char definition.
    std::uint32_t stringToken = fx.TypeDefToken(
        fx.mscorlibFile, "System", "String");
    std::uint32_t copyToStrToken = 0;
    for (const auto& m : fx.mscorlibFile.GetMethods(stringToken)) {
        if (m.Name == "CopyTo") { copyToStrToken = m.Token; break; }
    }
    ASSERT_NE(copyToStrToken, 0u);
    {
        TS::GenericContext emptyCtx(std::vector<const TS::ITypeParameter*>{});
        auto blob = fx.mscorlibFile.GetSignatureBlob(copyToStrToken);
        ASSERT_TRUE(blob.has_value());
        auto sig = decoder.DecodeMethodSignature(blob->data(), blob->size(),
            emptyCtx);
        EXPECT_EQ(sig.ReturnType->ReflectionName(), "System.Void");
        ASSERT_EQ(sig.ParameterTypes.size(), 4u);
        EXPECT_EQ(sig.ParameterTypes[0]->ReflectionName(), "System.Int32");
        auto* charArr = dynamic_cast<const TS::ArrayType*>(
            sig.ParameterTypes[1].get());
        ASSERT_NE(charArr, nullptr);
        EXPECT_EQ(charArr->ReflectionName(), "System.Char[]");
        EXPECT_EQ(sig.ParameterTypes[2]->ReflectionName(), "System.Int32");
        EXPECT_EQ(sig.ParameterTypes[3]->ReflectionName(), "System.Int32");
    }
}

// ---------------------------------------------------------------------------
// E: the crafted-blob matrix (gold sections E/F) through the port's walker:
// the FnPtr FromSignature arms (the CallConv table, the In/Out markers, the
// unmatched-modifier keep, the byref Ref promotion), the multi-dim array, the
// modreq/modopt field composites, the arity-mismatch instantiation, and the
// local-signature pinned element.
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, CraftedFnPtrMatrixMatchesGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();
    TS::GenericContext gc(std::vector<const TS::ITypeParameter*>{});
    TM::SignatureTypeProviderDecoder<TS::TypeProvider> decoder(
        provider, fx.mscorlibFile);

    // The mscorlib 4.8 marker TypeDefs the gold probe's crafted blobs encode
    // (CallConvCdecl/Stdcall/Thiscall/Fastcall, InAttribute, OutAttribute --
    // all REAL mscorlib TypeDefs, so the modifier resolution goes through
    // GetTypeFromDefinition and the IsKnownType marker check FIRES).
    std::uint32_t ccCdecl = fx.TypeDefToken(
        fx.mscorlibFile, "System.Runtime.CompilerServices", "CallConvCdecl");
    std::uint32_t ccStdcall = fx.TypeDefToken(
        fx.mscorlibFile, "System.Runtime.CompilerServices", "CallConvStdcall");
    std::uint32_t ccThiscall = fx.TypeDefToken(
        fx.mscorlibFile, "System.Runtime.CompilerServices", "CallConvThiscall");
    std::uint32_t ccFastcall = fx.TypeDefToken(
        fx.mscorlibFile, "System.Runtime.CompilerServices", "CallConvFastcall");
    std::uint32_t inAttr = fx.TypeDefToken(
        fx.mscorlibFile, "System.Runtime.InteropServices", "InAttribute");
    std::uint32_t outAttr = fx.TypeDefToken(
        fx.mscorlibFile, "System.Runtime.InteropServices", "OutAttribute");
    std::uint32_t stringDef = fx.TypeDefToken(
        fx.mscorlibFile, "System", "String");
    std::uint32_t listCoded = fx.TypeDefToken(
        fx.mscorlibFile, "System.Collections.Generic", "List`1");

    // The field-signature container minus its header byte: <type> where type
    // = 1B <embedded method sig> (the SRM field decoder checks the 0x06
    // header; the port's DecodeType decodes the bare type).
    auto decode = [&](std::vector<std::uint8_t> blob) -> TS::ITypePtr {
        return decoder.DecodeType(blob.data(), blob.size(), gc);
    };

    // F1: default convention, int return, string param.
    {
        TS::ITypePtr t = decode({ 0x1B, 0x00, 0x01, 0x08, 0x0E });
        auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
        ASSERT_NE(fp, nullptr) << "F1";
        EXPECT_EQ(fp->CallingConvention(),
                  TS::SignatureCallingConvention::Default);
        EXPECT_FALSE(fp->ReturnIsRefReadOnly());
        EXPECT_EQ(fp->CustomCallingConventions().size(), 0u);
        EXPECT_EQ(fp->ReturnType()->ReflectionName(), "System.Int32");
        ASSERT_EQ(fp->ParameterTypes().size(), 1u);
        EXPECT_EQ(fp->ParameterTypes()[0]->ReflectionName(),
                  "System.String");
        EXPECT_EQ(fp->ParameterReferenceKinds()[0],
                  TS::ReferenceKind::None);
    }
    // F2: the instance header -> the IntPtr fallback (NOT a function
    // pointer; the real IntPtr definition).
    {
        TS::ITypePtr t = decode({ 0x1B, 0x20, 0x01, 0x08, 0x0E });
        ASSERT_EQ(dynamic_cast<const TS::FunctionPointerType*>(t.get()),
                  nullptr);
        EXPECT_EQ(t->ReflectionName(), "System.IntPtr");
        EXPECT_EQ(t->Kind(), TS::TypeKind::Struct);
        EXPECT_EQ(t->GetDefinition()->MetadataToken(), 0x020000FDu);
    }
    // F3-F6: the unmanaged header + the CallConv modreq table (the FromSignature
    // calling-convention switch: Cdecl/Stdcall/ThisCall/FastCall).
    {
        struct Row { std::uint32_t token;
            TS::SignatureCallingConvention conv; };
        const Row rows[] = {
            { ccCdecl, TS::SignatureCallingConvention::CDecl },
            { ccStdcall, TS::SignatureCallingConvention::StdCall },
            { ccThiscall, TS::SignatureCallingConvention::ThisCall },
            { ccFastcall, TS::SignatureCallingConvention::FastCall },
        };
        for (const auto& row : rows) {
            std::vector<std::uint8_t> blob = { 0x1B, 0x09, 0x01, 0x1F };
            Append(blob, CodedForToken(row.token));
            Append(blob, { 0x08, 0x0E });
            TS::ITypePtr t = decode(blob);
            auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
            ASSERT_NE(fp, nullptr);
            EXPECT_EQ(fp->CallingConvention(), row.conv);
            EXPECT_FALSE(fp->ReturnIsRefReadOnly());
            EXPECT_EQ(fp->CustomCallingConventions().size(), 0u);
            EXPECT_EQ(fp->ReturnType()->ReflectionName(), "System.Int32");
            ASSERT_EQ(fp->ParameterTypes().size(), 1u);
            EXPECT_EQ(fp->ParameterReferenceKinds()[0],
                      TS::ReferenceKind::None);
        }
    }
    // F7: the CallConv modreq over a NON-unmanaged header -> the custom
    // calling convention list.
    {
        std::vector<std::uint8_t> blob = { 0x1B, 0x00, 0x01, 0x1F };
        Append(blob, CodedForToken(ccCdecl));
        Append(blob, { 0x08, 0x08 });
        TS::ITypePtr t = decode(blob);
        auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
        ASSERT_NE(fp, nullptr);
        EXPECT_EQ(fp->CallingConvention(),
                  TS::SignatureCallingConvention::Default);
        ASSERT_EQ(fp->CustomCallingConventions().size(), 1u);
        EXPECT_EQ(fp->CustomCallingConventions()[0]->ReflectionName(),
                  "System.Runtime.CompilerServices.CallConvCdecl");
    }
    // F8: the InAttribute modreq on the return -> returnIsRefReadOnly.
    {
        std::vector<std::uint8_t> blob = { 0x1B, 0x09, 0x00, 0x1F };
        Append(blob, CodedForToken(inAttr));
        blob.push_back(0x08);
        TS::ITypePtr t = decode(blob);
        auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
        ASSERT_NE(fp, nullptr);
        EXPECT_TRUE(fp->ReturnIsRefReadOnly());
        EXPECT_EQ(fp->CallingConvention(),
                  TS::SignatureCallingConvention::Unmanaged);
        EXPECT_EQ(fp->ReturnType()->ReflectionName(), "System.Int32");
        EXPECT_EQ(fp->ParameterTypes().size(), 0u);
    }
    // F9: the plain byref param -> the Ref promotion.
    {
        TS::ITypePtr t = decode({ 0x1B, 0x09, 0x01, 0x08, 0x10, 0x0E });
        auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
        ASSERT_NE(fp, nullptr);
        ASSERT_EQ(fp->ParameterTypes().size(), 1u);
        EXPECT_EQ(fp->ParameterTypes()[0]->Kind(),
                  TS::TypeKind::ByReference);
        EXPECT_EQ(fp->ParameterTypes()[0]->ReflectionName(),
                  "System.String&");
        EXPECT_EQ(fp->ParameterReferenceKinds()[0],
                  TS::ReferenceKind::Ref);
    }
    // F10/F11: the In/Out marker modreqs wrapping a byref param.
    {
        const std::pair<std::uint32_t, TS::ReferenceKind> rows[] = {
            { inAttr, TS::ReferenceKind::In },
            { outAttr, TS::ReferenceKind::Out },
        };
        for (const auto& row : rows) {
            std::vector<std::uint8_t> blob = { 0x1B, 0x09, 0x01, 0x08, 0x1F };
            Append(blob, CodedForToken(row.first));
            blob.push_back(0x10);
            blob.push_back(0x0E);
            TS::ITypePtr t = decode(blob);
            auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
            ASSERT_NE(fp, nullptr);
            ASSERT_EQ(fp->ParameterTypes().size(), 1u);
            // The marker is stripped: the parameter type is the bare ByRef.
            EXPECT_EQ(fp->ParameterTypes()[0]->Kind(),
                      TS::TypeKind::ByReference);
            EXPECT_EQ(fp->ParameterTypes()[0]->ReflectionName(),
                      "System.String&");
            EXPECT_EQ(fp->ParameterReferenceKinds()[0], row.second);
        }
    }
    // F12: the unmatched modifier on a param -> kind None, the modifier KEPT.
    {
        std::vector<std::uint8_t> blob = { 0x1B, 0x09, 0x01, 0x08, 0x1F };
        Append(blob, CodedForToken(stringDef));
        blob.push_back(0x08);
        TS::ITypePtr t = decode(blob);
        auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
        ASSERT_NE(fp, nullptr);
        ASSERT_EQ(fp->ParameterTypes().size(), 1u);
        EXPECT_EQ(fp->ParameterTypes()[0]->Kind(), TS::TypeKind::ModReq);
        EXPECT_EQ(fp->ParameterTypes()[0]->ReflectionName(),
                  "System.Int32 modreq(System.String)");
        EXPECT_EQ(fp->ParameterReferenceKinds()[0],
                  TS::ReferenceKind::None);
    }
    // F13: the unmatched modifier on the return -> the walk breaks, the
    // modifier kept on the return type.
    {
        std::vector<std::uint8_t> blob = { 0x1B, 0x09, 0x01, 0x1F };
        Append(blob, CodedForToken(stringDef));
        Append(blob, { 0x08, 0x08 });
        TS::ITypePtr t = decode(blob);
        auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get());
        ASSERT_NE(fp, nullptr);
        EXPECT_EQ(fp->ReturnType()->Kind(), TS::TypeKind::ModReq);
        EXPECT_EQ(fp->ReturnType()->ReflectionName(),
                  "System.Int32 modreq(System.String)");
        ASSERT_EQ(fp->ParameterTypes().size(), 1u);
        EXPECT_EQ(fp->ParameterTypes()[0]->ReflectionName(),
                  "System.Int32");
    }
    // E2/E3: the multi-dim arrays.
    {
        TS::ITypePtr t = decode({ 0x14, 0x08, 0x02, 0x01, 0x00, 0x00 });
        auto* a = dynamic_cast<const TS::ArrayType*>(t.get());
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->Rank(), 2);
        EXPECT_EQ(a->ReflectionName(), "System.Int32[,]");

        TS::ITypePtr t2 = decode({ 0x14, 0x08, 0x03, 0x02, 0x05, 0x06,
            0x03, 0x7F, 0x00, 0x62 });
        auto* a2 = dynamic_cast<const TS::ArrayType*>(t2.get());
        ASSERT_NE(a2, nullptr);
        EXPECT_EQ(a2->Rank(), 3);
        EXPECT_EQ(a2->ReflectionName(), "System.Int32[,,]");
    }
    // E4/E5: the modreq/modopt field composites.
    {
        std::vector<std::uint8_t> blob = { 0x1F };
        Append(blob, CodedForToken(stringDef));
        blob.push_back(0x08);
        TS::ITypePtr t = decode(blob);
        auto* m = dynamic_cast<const TS::ModifiedType*>(t.get());
        ASSERT_NE(m, nullptr);
        EXPECT_TRUE(m->IsRequired());
        EXPECT_EQ(m->ReflectionName(),
                  "System.Int32 modreq(System.String)");

        std::vector<std::uint8_t> blob2 = { 0x20 };
        Append(blob2, CodedForToken(stringDef));
        blob2.push_back(0x08);
        TS::ITypePtr t2 = decode(blob2);
        auto* m2 = dynamic_cast<const TS::ModifiedType*>(t2.get());
        ASSERT_NE(m2, nullptr);
        EXPECT_FALSE(m2->IsRequired());
        EXPECT_EQ(m2->ReflectionName(),
                  "System.Int32 modopt(System.String)");
    }
    // E6/E7: the arity-mismatch and matching generic instantiations.
    {
        std::vector<std::uint8_t> blob = { 0x15, 0x12 };
        Append(blob, CodedForToken(listCoded));
        Append(blob, { 0x02, 0x08, 0x0E });
        TS::ITypePtr t = decode(blob);
        // 2 args on a tpc-1 generic: the generic itself.
        EXPECT_EQ(t->ReflectionName(),
                  "System.Collections.Generic.List`1");
        EXPECT_EQ(t->Kind(), TS::TypeKind::Class);

        std::vector<std::uint8_t> blob2 = { 0x15, 0x12 };
        Append(blob2, CodedForToken(listCoded));
        Append(blob2, { 0x01, 0x08 });
        TS::ITypePtr t2 = decode(blob2);
        auto* pt = dynamic_cast<const TS::ParameterizedType*>(t2.get());
        ASSERT_NE(pt, nullptr);
        EXPECT_EQ(pt->GenericType()->ReflectionName(),
                  "System.Collections.Generic.List`1");
        ASSERT_EQ(pt->TypeArguments().size(), 1u);
        EXPECT_EQ(pt->TypeArguments()[0]->ReflectionName(),
                  "System.Int32");
    }
    // E8: the local signature -- the pinned element (header 0x07, 2 locals:
    // int32 and pinned int32).
    {
        std::vector<std::uint8_t> blob = { 0x07, 0x02, 0x08, 0x45, 0x08 };
        auto locals = decoder.DecodeLocalSignature(blob.data(), blob.size(),
            gc);
        ASSERT_EQ(locals.size(), 2u);
        EXPECT_EQ(locals[0]->ReflectionName(), "System.Int32");
        auto* pin = dynamic_cast<const TS::PinnedType*>(locals[1].get());
        ASSERT_NE(pin, nullptr);
        EXPECT_EQ(pin->ReflectionName(), "System.Int32 pinned");
        EXPECT_EQ(pin->Element()->ReflectionName(), "System.Int32");
    }
    // E9: the pointer over a generic instantiation.
    {
        std::vector<std::uint8_t> blob = { 0x0F, 0x15, 0x12 };
        Append(blob, CodedForToken(listCoded));
        Append(blob, { 0x01, 0x08 });
        TS::ITypePtr t = decode(blob);
        auto* p = dynamic_cast<const TS::PointerType*>(t.get());
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->Kind(), TS::TypeKind::Pointer);
        EXPECT_EQ(IsRefStr(*p), "null");
        auto* pt = dynamic_cast<const TS::ParameterizedType*>(
            p->Element().get());
        ASSERT_NE(pt, nullptr);
        ASSERT_EQ(pt->TypeArguments().size(), 1u);
        EXPECT_EQ(pt->TypeArguments()[0]->ReflectionName(),
                  "System.Int32");
    }
}

// ---------------------------------------------------------------------------
// F1/F2: the definition and specification resolution arms (gold sections
// F1/F2). GetTypeFromDefinition returns the module's own entity for every
// rawTypeKind (the raw byte only matters for the unreachable UnknownType
// fallback); GetTypeFromSpecification decodes the InterfaceImpl TypeSpec
// (IList`1<T> over the List`1 context).
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, DefinitionAndSpecificationResolutionMatchGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();

    // F1: the raw byte never changes the resolution (the module-backed
    // provider always resolves its own rows).
    std::uint32_t stringToken = fx.TypeDefToken(fx.mscorlibFile,
        "System", "String");
    std::uint32_t intToken = fx.TypeDefToken(fx.mscorlibFile,
        "System", "Int32");
    std::uint32_t comparableToken = fx.TypeDefToken(fx.mscorlibFile,
        "System", "IComparable`1");
    for (std::uint8_t raw : { 0x12, 0x11, 0x00 }) {
        TS::ITypePtr t = provider.GetTypeFromDefinition(stringToken, raw);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Class) << raw;
        EXPECT_EQ(t->Name(), "String") << raw;
        EXPECT_EQ(t->ReflectionName(), "System.String") << raw;
        EXPECT_EQ(t->GetDefinition()->MetadataToken(), stringToken) << raw;
        // The definition IS the resolved type (the gold's isDef=True).
        EXPECT_EQ(static_cast<const TS::IType*>(t->GetDefinition()),
                  t.get()) << raw;
    }
    {
        TS::ITypePtr t = provider.GetTypeFromDefinition(intToken, 0x11);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Struct);
        EXPECT_EQ(t->GetDefinition()->MetadataToken(), 0x020000FBu);
    }
    {
        TS::ITypePtr t = provider.GetTypeFromDefinition(comparableToken, 0x12);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Interface);
        EXPECT_EQ(t->ReflectionName(), "System.IComparable`1");
    }

    // F2: the List`1 IList`1 InterfaceImpl TypeSpec -- the specification
    // decodes through the provider into the parameterized interface over the
    // context's VAR.
    std::uint32_t listToken = fx.TypeDefToken(fx.mscorlibFile,
        "System.Collections.Generic", "List`1");
    std::uint32_t typeSpecToken = 0;
    for (const auto& row :
         fx.mscorlibFile.GetInterfaceImplementations(listToken)) {
        if ((row.InterfaceToken >> 24) == 0x1B) {
            typeSpecToken = row.InterfaceToken;
            break;
        }
    }
    ASSERT_NE(typeSpecToken, 0u);
    const TS::ITypeDefinition* listDef =
        fx.mscorlibModule->GetDefinition(listToken);
    TS::GenericContext gc(listDef->TypeParameters());
    TS::ITypePtr t = provider.GetTypeFromSpecification(
        typeSpecToken, 0x00, gc);
    auto* pt = dynamic_cast<const TS::ParameterizedType*>(t.get());
    ASSERT_NE(pt, nullptr);
    EXPECT_EQ(pt->Kind(), TS::TypeKind::Interface);
    EXPECT_EQ(pt->GenericType()->ReflectionName(),
              "System.Collections.Generic.IList`1");
    ASSERT_EQ(pt->TypeArguments().size(), 1u);
    auto* tp = dynamic_cast<const TS::ITypeParameter*>(
        pt->TypeArguments()[0].get());
    ASSERT_NE(tp, nullptr);
    EXPECT_EQ(tp->Name(), "T");
    EXPECT_EQ(tp->Index(), 0);

    // The invalid-token throw (the C# BadImageFormatException shape).
    EXPECT_THROW(provider.GetTypeFromSpecification(0x1BFFFFFF, 0x00, gc),
                 std::out_of_range);
}

// ---------------------------------------------------------------------------
// F3: the TypeRef resolution arms (gold section F3) over the System.dll
// module's provider: the resolvable cross-module reference (System.Object ->
// the mscorlib definition via the declaring module), the unresolvable one
// (System.Configuration.ConfigurationElement -> the UnknownType fallback with
// the reference-ness from the raw byte), and the nested declaring scope
// (DebuggingModes -> the mscorlib DebuggableAttribute+DebuggingModes
// definition).
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, TypeRefResolutionMatchGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& systemProvider =
        const_cast<TS::TypeProvider&>(fx.systemModule->TypeProvider());

    auto findTypeRef = [&](const char* ns, const char* name,
                           bool nested) -> std::uint32_t {
        for (std::uint32_t i = 1;
             i <= fx.systemFile.TypeRefCount(); ++i) {
            auto info = fx.systemFile.GetTypeRefNameInfo(
                (0x01u << 24) | i);
            if (!info) continue;
            bool isNested = info->DeclaringTypeRefToken != 0;
            if (isNested != nested) continue;
            if (info->Namespace == ns && info->Name == name)
                return (0x01u << 24) | i;
        }
        return 0;
    };

    // The resolvable reference: System.Object (an AssemblyRef-scoped TypeRef
    // into the loaded mscorlib). The gold: kind=Class for every raw byte.
    std::uint32_t objectRef = findTypeRef("System", "Object", false);
    ASSERT_NE(objectRef, 0u);
    for (std::uint8_t raw : { 0x12, 0x11, 0x00 }) {
        TS::ITypePtr t = systemProvider.GetTypeFromReference(objectRef, raw);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Class) << raw;
        EXPECT_EQ(t->ReflectionName(), "System.Object") << raw;
        EXPECT_EQ(t->GetDefinition()->MetadataToken(), 0x0200003Du) << raw;
    }

    // The unresolvable reference: System.Configuration.ConfigurationElement
    // (the AssemblyRef target is not in the compilation) -> the UnknownType
    // fallback with the full name and the raw-byte reference-ness (the gold:
    // 0x12 -> isRef=True, 0x11 -> False, 0x00 -> null).
    std::uint32_t configRef = findTypeRef("System.Configuration",
        "ConfigurationElement", false);
    ASSERT_NE(configRef, 0u);
    struct RawRow { std::uint8_t raw; bool expect; bool hasValue; };
    for (const auto& row : {
        RawRow{ 0x12, true, true }, RawRow{ 0x11, false, true },
        RawRow{ 0x00, false, false } }) {
        TS::ITypePtr t = systemProvider.GetTypeFromReference(configRef,
            row.raw);
        // The elaborated `class` form: the free function UnknownType() hides
        // the class name (the null-object factory collision).
        auto* u = dynamic_cast<
            const class ILSpy::Decompiler::TypeSystem::UnknownType*>(
            t.get());
        ASSERT_NE(u, nullptr) << row.raw;
        EXPECT_EQ(t->Kind(), TS::TypeKind::Unknown) << row.raw;
        EXPECT_EQ(t->Name(), "ConfigurationElement") << row.raw;
        EXPECT_EQ(t->ReflectionName(),
                  "System.Configuration.ConfigurationElement") << row.raw;
        EXPECT_EQ(u->FullTypeName().ReflectionName(),
                  "System.Configuration.ConfigurationElement") << row.raw;
        auto isRef = t->IsReferenceType();
        if (row.hasValue) {
            ASSERT_TRUE(isRef.has_value()) << row.raw;
            EXPECT_EQ(*isRef, row.expect) << row.raw;
        } else {
            EXPECT_FALSE(isRef.has_value()) << row.raw;
        }
    }

    // The nested declaring scope: DebuggingModes (a TypeRef whose resolution
    // scope is the DebuggableAttribute TypeRef) -> the nested mscorlib
    // definition through the declaring-chain + nested-walk (the gold:
    // kind=Enum, reflection
    // System.Diagnostics.DebuggableAttribute+DebuggingModes).
    std::uint32_t modesRef = findTypeRef("", "DebuggingModes", true);
    ASSERT_NE(modesRef, 0u);
    TS::ITypePtr t = systemProvider.GetTypeFromReference(modesRef, 0x12);
    EXPECT_EQ(t->Kind(), TS::TypeKind::Enum);
    EXPECT_EQ(t->Name(), "DebuggingModes");
    EXPECT_EQ(t->ReflectionName(),
              "System.Diagnostics.DebuggableAttribute+DebuggingModes");
    EXPECT_EQ(t->GetDefinition()->MetadataToken(), 0x02000B82u);
}

// ---------------------------------------------------------------------------
// G: the attribute-provider arms (gold section G).
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, AttributeProviderArmsMatchGold)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();

    // G1: GetSystemType -> the mscorlib System.Type definition.
    TS::ITypePtr sys = provider.GetSystemType();
    ASSERT_NE(sys, nullptr);
    EXPECT_EQ(sys->Kind(), TS::TypeKind::Class);
    EXPECT_EQ(sys->Name(), "Type");
    EXPECT_EQ(sys->ReflectionName(), "System.Type");
    EXPECT_EQ(sys->GetDefinition()->KnownTypeCode(),
              TS::KnownTypeCode::Type);

    // G2: IsSystemType.
    TS::ITypePtr int32 = provider.GetPrimitiveType(
        TM::PrimitiveTypeCode::Int32);
    EXPECT_TRUE(provider.IsSystemType(*sys));
    EXPECT_FALSE(provider.IsSystemType(*int32));

    // G3: GetTypeFromSerializedName.
    {
        TS::ITypePtr t = provider.GetTypeFromSerializedName("System.String");
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Class);
        EXPECT_EQ(t->ReflectionName(), "System.String");
        EXPECT_EQ(t->GetDefinition()->MetadataToken(), 0x02000073u);
    }
    {
        TS::ITypePtr t = provider.GetTypeFromSerializedName(
            "System.Collections.Generic.List`1[[System.String]]");
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Class);
        EXPECT_EQ(t->Name(), "List");
        EXPECT_EQ(t->TypeParameterCount(), 1);
    }
    {
        // The nested form resolves the Dictionary`2+Enumerator definition.
        TS::ITypePtr t = provider.GetTypeFromSerializedName(
            "System.Collections.Generic.Dictionary`2+Enumerator");
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Struct);
        EXPECT_EQ(t->Name(), "Enumerator");
        EXPECT_EQ(t->ReflectionName(),
                  "System.Collections.Generic.Dictionary`2+Enumerator");
        EXPECT_EQ(t->TypeParameterCount(), 2);
    }
    {
        // The miss -> the UnknownType.
        TS::ITypePtr t = provider.GetTypeFromSerializedName(
            "System.NoSuchType");
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Unknown);
        EXPECT_EQ(t->Name(), "NoSuchType");
        EXPECT_EQ(t->ReflectionName(), "System.NoSuchType");
    }
    {
        // The array form.
        TS::ITypePtr t = provider.GetTypeFromSerializedName(
            "System.Int32[]");
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->Kind(), TS::TypeKind::Array);
        EXPECT_EQ(t->ReflectionName(), "System.Int32[]");
    }
    {
        // The parse failure: the C# BadImageFormatException rethrow carrying
        // the exact formatted message (the gold's G3).
        try {
            provider.GetTypeFromSerializedName("bad[name");
            FAIL() << "expected the invalid_argument";
        } catch (const std::invalid_argument& ex) {
            EXPECT_STREQ(ex.what(),
                "Invalid type name: \"bad[name\": Invalid type name: bad[name");
        }
    }

    // G4: GetUnderlyingEnumType.
    {
        // The real ConsoleColor enum: the underlying Int32 -> the Int32
        // primitive code (mscorlib 4.8's ConsoleColor is int-backed).
        std::uint32_t token = fx.TypeDefToken(fx.mscorlibFile,
            "System", "ConsoleColor");
        const TS::ITypeDefinition* def = fx.mscorlibModule->GetDefinition(
            token);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(provider.GetUnderlyingEnumType(*def),
                  TM::PrimitiveTypeCode::Int32);
    }
    {
        // A non-enum passthrough: Int32 itself.
        std::uint32_t token = fx.TypeDefToken(fx.mscorlibFile,
            "System", "Int32");
        const TS::ITypeDefinition* def = fx.mscorlibModule->GetDefinition(
            token);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(provider.GetUnderlyingEnumType(*def),
                  TM::PrimitiveTypeCode::Int32);
    }
    {
        // An interface (a non-primitive non-enum): the Unknown code 0.
        std::uint32_t token = fx.TypeDefToken(fx.mscorlibFile,
            "System", "IAsyncResult");
        const TS::ITypeDefinition* def = fx.mscorlibModule->GetDefinition(
            token);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(provider.GetUnderlyingEnumType(*def),
                  static_cast<TM::PrimitiveTypeCode>(0));
    }
    {
        // An unresolved type: the EnumUnderlyingTypeResolveException with
        // the .NET default-form message (the gold's G4).
        TS::ITypePtr unknown = provider.GetTypeFromSerializedName(
            "System.NoSuchType");
        try {
            provider.GetUnderlyingEnumType(*unknown);
            FAIL() << "expected the exception";
        } catch (const TM::EnumUnderlyingTypeResolveException& ex) {
            EXPECT_STREQ(ex.what(),
                "Exception of type 'ICSharpCode.Decompiler.Metadata."
                "EnumUnderlyingTypeResolveException' was thrown.");
        }
    }
}

// ---------------------------------------------------------------------------
// F4: the compilation-only provider (gold section F4). The C# reads through
// the CALLER's reader (the fallback builds an UnknownType with the
// reference-ness from the raw byte -- the gold pins def12->isRef=true,
// def11->false, def00->null); the port's provider contract derives every
// metadata read from the provider's own module, so the reader-dependent arms
// throw the documented loud logic_error instead. The reader-independent arms
// (the composites, the primitives, the serialized-name parser) keep working.
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, CompilationOnlyProviderDivergence)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider only(fx.compilation);

    EXPECT_EQ(&only.Compilation(), &fx.compilation);
    // The primitives keep working (the compilation's FindType).
    TS::ITypePtr int32 = only.GetPrimitiveType(
        TM::PrimitiveTypeCode::Int32);
    EXPECT_EQ(int32->ReflectionName(), "System.Int32");
    // The serialized-name parser keeps working (the compilation context).
    TS::ITypePtr t = only.GetTypeFromSerializedName("System.String");
    EXPECT_EQ(t->ReflectionName(), "System.String");
    // GetSystemType keeps working.
    EXPECT_EQ(only.GetSystemType()->ReflectionName(), "System.Type");

    // The reader-dependent arms throw the documented divergence.
    EXPECT_THROW(only.GetTypeFromDefinition(0x02000073, 0x12),
                 std::logic_error);
    EXPECT_THROW(only.GetTypeFromReference(0x01000001, 0x12),
                 std::logic_error);
    EXPECT_THROW(only.GetTypeFromSpecification(0x1B000001, 0x00,
        TS::GenericContext(std::vector<const TS::ITypeParameter*>{})),
        std::logic_error);
}

// ---------------------------------------------------------------------------
// The module-owned provider identity: the accessor returns the ctor-built
// instance (the C# `internal readonly TypeProvider TypeProvider` field).
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, ModuleOwnsTheProvider)
{
    MscorlibSystemFixture fx;
    const TS::TypeProvider& p1 = fx.mscorlibModule->TypeProvider();
    const TS::TypeProvider& p2 = fx.mscorlibModule->TypeProvider();
    EXPECT_EQ(&p1, &p2);
    EXPECT_EQ(&p1.Compilation(), &fx.compilation);
    // Each module owns its own provider.
    EXPECT_NE(&fx.mscorlibModule->TypeProvider(),
              &fx.systemModule->TypeProvider());
}

// ---------------------------------------------------------------------------
// The PinnedType VisitChildren reconstruction (the TypeVisitor dispatch): an
// unchanged element returns the same instance; the substitution visitor
// leaves the pinned primitive wrapper unchanged.
// ---------------------------------------------------------------------------
TEST(TypeProviderTest, PinnedTypeVisitorDispatch)
{
    MscorlibSystemFixture fx;
    TS::TypeProvider& provider = fx.Provider();
    TS::ITypePtr int32 = provider.GetPrimitiveType(
        TM::PrimitiveTypeCode::Int32);
    // shared_ptr-owned (the C# `return this` ports to shared_from_this, so
    // the visited type must be shared_ptr-owned).
    auto pinned = std::make_shared<TS::PinnedType>(int32);
    // The null substitution leaves the pinned type unchanged (the C#
    // VisitOtherType default -> VisitChildren -> unchanged -> this).
    TS::TypeParameterSubstitution subst(std::nullopt, std::nullopt);
    TS::ITypePtr visited = pinned->AcceptVisitor(subst);
    EXPECT_EQ(visited.get(), pinned.get());
}
