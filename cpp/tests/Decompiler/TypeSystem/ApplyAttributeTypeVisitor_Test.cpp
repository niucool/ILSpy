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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Gold-pinned tests for ApplyAttributeTypeVisitor (the C#
// TypeSystem/ApplyAttributeTypeVisitor.cs port): every drive's expectation
// comes from the REAL installed ICSharpCode.Decompiler 11.0 internal visitor
// driven via reflection over the IDENTICAL inputs (the AatProbe gold,
// cpp/tests/TestFixtures/AatVisitorGold.hpp -- 41 drives: S01..S35 over the
// metadata entry, P01..P06 over the PDB entry). The drives cover:
//   - the tuple machinery over the real mscorlib ValueTuple`1..`8 definitions
//     (the 2-tuple, the 15-element 7+(7+1) and 10-element 7+3 eight-ary
//     nestings, the non-tuple Rest fall-through, cardinality 1, non-ValueTuple
//     generic types, the element-name extraction incl. the null tail),
//   - the dynamic-flag index walk (direct object, List<object> args, tuple
//     elements, the modopt unwrap, the empty and MethodDef-ctor rows),
//   - the nullability index walk (the generic-struct dummy slot, List<string>
//     positions, tuple elements, arrays incl. the 2-dim shape, the
//     function-pointer return/parameter slots with the in/out double-slot
//     rule, the single-byte default form),
//   - native integers (the attribute row and the NativeIntegersWithoutAttribute
//     flag), the KeepModifiers arms, typeChildrenOnly, the no-options
//     short-circuit (reference identity), the whole-entity accumulation drive
//     (every attribute row on one parent -- the later rows override the
//     earlier ones), and the additionalAttributes override.
// The port test rebuilds the probe's input types over the port's real
// MetadataModule-backed SimpleCompilation (the TypeProvider_Test fixture
// shape), drives the port visitor the same way, and compares the recursive
// dumps byte-for-byte with the gold triples.

#include "TestFixtures/AatVisitorGold.hpp"

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ApplyAttributeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
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

// ---------------------------------------------------------------------------
// The fixture: the port's SimpleCompilation of the real mscorlib (main, no
// references -- the probe's `new SimpleCompilation(new PEFile(MscorlibPath()))`),
// the synthetic-manifest MetadataFile (the AatSynth.dll bytes written to a
// temp file), and the field-parent attribute-token collections.
// ---------------------------------------------------------------------------

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

class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

std::string WriteSynthManifest()
{
    const char* tempDir = std::getenv("TEMP");
    std::string dir = tempDir ? tempDir : ".";
    std::string path = dir + "\\ilspy_aat_synth_test.dll";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return {};
    out.write(reinterpret_cast<const char*>(ILSpy::Tests::kAatSynthManifest),
              ILSpy::Tests::kAatSynthManifestSize);
    out.close();
    if (!out)
        return {};
    return path;
}

struct AatFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    std::string synthPath = WriteSynthManifest();
    TM::MetadataFile synthFile{ synthPath };
    TestCompilation compilation;
    std::unique_ptr<TS::MetadataModule> mscorlibModule;
    FixedModuleRef mscorlibRef;

    AatFixture() {
        mscorlibModule = std::make_unique<TS::MetadataModule>(
            compilation, &mscorlibFile, TS::TypeSystemOptions::Default);
        mscorlibRef = FixedModuleRef(mscorlibModule.get());
        compilation.Initialize(mscorlibRef, {});
    }

    // The C# probe's `comp.FindType(code)` -- the port's ICompilation returns
    // `const IType&`; the drives need the owning ITypePtr handle (the
    // const_pointer_cast shared_from_this bridge, the NullableType precedent).
    TS::ITypePtr KnownType(TS::KnownTypeCode code) {
        const TS::IType& type = compilation.FindType(code);
        return std::const_pointer_cast<TS::IType>(type.shared_from_this());
    }

    // The C# probe's `comp.FindType(new TopLevelTypeName(...))` -- the port's
    // free FindType over FullTypeName (the modules scan + UnknownType
    // fallback).
    TS::ITypePtr FindType(const char* ns, const char* name, int tpc) {
        return TS::FindType(compilation,
            TS::FullTypeName(TS::TopLevelTypeName(ns, name, tpc)));
    }

    // The C# probe's `Attrs(fieldHandle)` -- the CustomAttributeHandleCollection
    // of a field parent. The AatSynth row plan (the probe's Program.cs):
    // fDyn1=1..fAll=10, so the field tokens are 0x04000001..0x0400000A.
    std::vector<std::uint32_t> Attrs(int fieldRow) {
        return synthFile.GetCustomAttributeTokens(0x04000000u + fieldRow);
    }
};

// ---------------------------------------------------------------------------
// The dump: the probe's compact recursive render, byte-for-byte. The C# prints
// `t.Kind` / `t.Name` / `t.Nullability` (the enum member names -- the port's
// enum members carry the same names, rendered through the local tables) and
// the per-shape payloads.
// ---------------------------------------------------------------------------

std::string Dump(const TS::ITypePtr& t, int depth = 0);

std::string TypeKindName(TS::TypeKind kind)
{
    switch (kind) {
        case TS::TypeKind::Struct: return "Struct";
        case TS::TypeKind::Class: return "Class";
        case TS::TypeKind::Tuple: return "Tuple";
        case TS::TypeKind::Dynamic: return "Dynamic";
        case TS::TypeKind::NInt: return "NInt";
        case TS::TypeKind::NUInt: return "NUInt";
        case TS::TypeKind::Array: return "Array";
        case TS::TypeKind::ModOpt: return "ModOpt";
        case TS::TypeKind::ModReq: return "ModReq";
        case TS::TypeKind::FunctionPointer: return "FunctionPointer";
        case TS::TypeKind::Unknown: return "Unknown";
        default: return std::to_string(static_cast<int>(kind));
    }
}

std::string NullabilityName(TS::Nullability n)
{
    switch (n) {
        case TS::Nullability::Oblivious: return "Oblivious";
        case TS::Nullability::NotNullable: return "NotNullable";
        case TS::Nullability::Nullable: return "Nullable";
    }
    return "?";
}

std::string KnownTypeCodeName(TS::KnownTypeCode code)
{
    switch (code) {
        case TS::KnownTypeCode::None: return "None";
        case TS::KnownTypeCode::Object: return "Object";
        case TS::KnownTypeCode::String: return "String";
        case TS::KnownTypeCode::IntPtr: return "IntPtr";
        case TS::KnownTypeCode::UIntPtr: return "UIntPtr";
        default: return std::to_string(static_cast<int>(code));
    }
}

std::string ReferenceKindName(TS::ReferenceKind kind)
{
    switch (kind) {
        case TS::ReferenceKind::None: return "None";
        case TS::ReferenceKind::Ref: return "Ref";
        case TS::ReferenceKind::Out: return "Out";
        case TS::ReferenceKind::In: return "In";
        case TS::ReferenceKind::RefReadOnly: return "RefReadOnly";
    }
    return "?";
}

// The C# `t.Name` over the shapes the drives reach. The port's
// ArrayType/ModifiedType Name renders are the flattened minimal-port forms
// (the element name without the suffix), so the dump composes the C# shapes
// (the TypeWithElementType `ElementType.Name + NameSuffix` rules) locally:
// an ArrayType appends `"[" + (rank-1 commas) + "]"`; a ModifiedType appends
// `" modopt(" + Modifier.ReflectionName + ")"` (the C# NameSuffix renders the
// modifier's ToString == ReflectionName).
std::string CSharpName(const TS::IType& t)
{
    if (const auto* at = dynamic_cast<const TS::ArrayType*>(&t)) {
        std::string suffix = "[";
        for (int i = 1; i < at->Rank(); ++i)
            suffix += ",";
        suffix += "]";
        return at->Element() ? CSharpName(*at->Element()) + suffix : std::string("?");
    }
    if (const auto* mt = dynamic_cast<const TS::ModifiedType*>(&t)) {
        std::string modifier = mt->Modifier() ? mt->Modifier()->ReflectionName() : "?";
        return (mt->Element() ? CSharpName(*mt->Element()) : std::string("?"))
            + (mt->IsRequired() ? " modreq(" : " modopt(") + modifier + ")";
    }
    return t.Name();
}

std::string Dump(const TS::ITypePtr& t, int depth)
{
    if (!t)
        return "null";
    if (depth > 10)
        return "deep";
    std::string b = TypeKindName(t->Kind()) + ":" + CSharpName(*t)
        + ":N=" + NullabilityName(t->Nullability());
    if (const auto* tuple = dynamic_cast<const TS::TupleType*>(t.get())) {
        b += ":card=" + std::to_string(tuple->Cardinality());
        b += ":names=";
        std::string names;
        for (const std::string& n : tuple->ElementNames()) {
            if (!names.empty())
                names += ",";
            names += n.empty() ? "<null>" : n;
        }
        b += names;
        b += ":under[" + Dump(tuple->UnderlyingType(), depth + 1) + "]";
    }
    if (const auto* pt = dynamic_cast<const TS::ParameterizedType*>(t.get())) {
        b += ":gen[" + Dump(pt->GenericType(), depth + 1) + "]";
        b += ":args[";
        std::string args;
        for (const TS::ITypePtr& a : pt->TypeArguments()) {
            if (!args.empty())
                args += ";";
            args += Dump(a, depth + 1);
        }
        b += args + "]";
    }
    if (const auto* at = dynamic_cast<const TS::ArrayType*>(t.get())) {
        b += ":dims=" + std::to_string(at->Rank());
        b += ":el[" + Dump(at->Element(), depth + 1) + "]";
    }
    if (const auto* mt = dynamic_cast<const TS::ModifiedType*>(t.get())) {
        b += ":mod[" + (mt->Modifier() ? mt->Modifier()->Name() : std::string("?")) + "]";
        b += ":el[" + Dump(mt->Element(), depth + 1) + "]";
    }
    if (const auto* na = dynamic_cast<const TS::NullabilityAnnotatedType*>(t.get())) {
        b += ":wrapped[" + Dump(na->TypeWithoutAnnotation(), depth + 1) + "]";
    }
    if (const auto* fp = dynamic_cast<const TS::FunctionPointerType*>(t.get())) {
        b += ":ret[" + Dump(fp->ReturnType(), depth + 1) + "]";
        b += ":ps[";
        std::string ps;
        for (std::size_t i = 0; i < fp->ParameterTypes().size(); ++i) {
            if (!ps.empty())
                ps += ";";
            ps += ReferenceKindName(fp->ParameterReferenceKinds()[i])
                + ":" + Dump(fp->ParameterTypes()[i], depth + 1);
        }
        b += ps + "]";
    }
    if (const auto* td = dynamic_cast<const TS::ITypeDefinition*>(t.get())) {
        b += ":ktc=" + KnownTypeCodeName(td->KnownTypeCode());
    }
    if (const auto* ut = dynamic_cast<const class TS::UnknownType*>(t.get())) {
        b += ":full=" + ut->FullTypeName().ReflectionName();
    }
    if (const auto* br = dynamic_cast<const TS::ByReferenceType*>(t.get())) {
        b += ":el[" + Dump(br->Element(), depth + 1) + "]";
    }
    if (const auto* po = dynamic_cast<const TS::PointerType*>(t.get())) {
        b += ":el[" + Dump(po->Element(), depth + 1) + "]";
    }
    return b;
}

// ---------------------------------------------------------------------------
// The drive helpers
// ---------------------------------------------------------------------------

const ILSpy::Tests::AatGoldDrive& Gold(const char* id)
{
    for (const auto& drive : ILSpy::Tests::kAatGoldDrives) {
        if (drive.id == std::string(id))
            return drive;
    }
    // A missing gold row is a fixture-generation break, not a pass.
    static ILSpy::Tests::AatGoldDrive missing{ "<missing>", "", "", false };
    ADD_FAILURE() << "missing gold drive: " << id;
    return missing;
}

using AttrsOpt = std::optional<std::vector<std::uint32_t>>;

// The metadata entry over a gold drive: dumps the input (pinning the fixture
// construction against the gold IN), drives the port visitor, dumps the
// result, and checks the reference-identity contract.
void Drive1(const char* id, const TS::ITypePtr& input, TS::ICompilation& compilation,
           const TM::MetadataFile& metadata, TS::TypeSystemOptions options,
           TS::Nullability nctx, const AttrsOpt& attrs, bool childrenOnly = false,
           const AttrsOpt& additional = std::nullopt)
{
    const auto& gold = Gold(id);
    ASSERT_EQ(Dump(input), gold.in);
    TS::ITypePtr result = TS::ApplyAttributeTypeVisitor::ApplyAttributesToType(
        input, compilation, attrs, metadata, options, nctx, childrenOnly, additional);
    EXPECT_EQ(Dump(result), gold.out);
    EXPECT_EQ(result.get() == input.get(), gold.same);
}

// The PDB entry over a gold drive.
void Drive2(const char* id, const TS::ITypePtr& input, TS::ICompilation& compilation,
           TS::TypeSystemOptions options, const ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo& pdb)
{
    const auto& gold = Gold(id);
    ASSERT_EQ(Dump(input), gold.in);
    TS::ITypePtr result = TS::ApplyAttributeTypeVisitor::ApplyAttributesToType(
        input, compilation, options, pdb);
    EXPECT_EQ(Dump(result), gold.out);
    EXPECT_EQ(result.get() == input.get(), gold.same);
}

TS::ITypePtr PT(const TS::ITypePtr& generic, std::vector<TS::ITypePtr> args)
{
    return std::make_shared<TS::ParameterizedType>(generic, std::move(args));
}

} // namespace

// ---------------------------------------------------------------------------
// TupleReconstruction: the 2/10/15-element ValueTuple nestings, the names,
// the non-tuple Rest fall-through, cardinality 1, non-tuple generics
// (S01..S08)
// ---------------------------------------------------------------------------

TEST(ApplyAttributeTypeVisitorTest, TupleReconstructionDrives)
{
    AatFixture f;
    ASSERT_TRUE(f.synthFile.IsValid()) << "the AatSynth manifest must parse";
    auto str = f.KnownType(TS::KnownTypeCode::String);
    auto obj = f.KnownType(TS::KnownTypeCode::Object);
    auto vt1 = f.FindType("System", "ValueTuple", 1);
    auto vt2 = f.FindType("System", "ValueTuple", 2);
    auto vt3 = f.FindType("System", "ValueTuple", 3);
    auto vt8 = f.FindType("System", "ValueTuple", 8);
    auto list = f.FindType("System.Collections.Generic", "List", 1);

    auto vt2StrObj = PT(vt2, { str, obj });
    auto vt2StrStr = PT(vt2, { str, str });
    auto listObj = PT(list, { obj });
    auto vt1Obj = PT(vt1, { obj });
    auto inner8 = PT(vt8, { str, str, str, str, str, str, str, vt1Obj });
    auto outer15 = PT(vt8, { str, str, str, str, str, str, str, inner8 });
    auto vt3StrObjStr = PT(vt3, { str, obj, str });
    auto outer10 = PT(vt8, { str, str, str, str, str, str, str, vt3StrObjStr });
    auto outer7Bad = PT(vt8, { str, str, str, str, str, str, str, listObj });

    const auto Tup = TS::TypeSystemOptions::Tuple;
    const auto NAnn = TS::TypeSystemOptions::NullabilityAnnotations;

    // (fTup is the 4th field row: the [TupleElementNames(new[]{"a", null, "c"})] parent.)
    Drive1("S01", vt2StrObj, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, std::nullopt);
    Drive1("S02", vt2StrObj, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, f.Attrs(4));
    Drive1("S03", vt2StrStr, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, f.Attrs(4));
    Drive1("S04", outer15, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, f.Attrs(4));
    Drive1("S05", outer10, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, f.Attrs(4));
    Drive1("S06", outer7Bad, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, f.Attrs(4));
    Drive1("S07", vt1Obj, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, f.Attrs(4));
    Drive1("S08", listObj, f.compilation, f.synthFile, Tup, TS::Nullability::Oblivious, f.Attrs(4));

    // The tuple-name extraction arms against the S02/S04/S05 results.
    {
        TS::ITypePtr result = TS::ApplyAttributeTypeVisitor::ApplyAttributesToType(
            vt2StrObj, f.compilation, f.Attrs(4), f.synthFile, Tup,
            TS::Nullability::Oblivious);
        const auto* tuple = dynamic_cast<const TS::TupleType*>(result.get());
        ASSERT_NE(tuple, nullptr);
        EXPECT_EQ(tuple->ElementNames(), (std::vector<std::string>{ "a", "" }));
        // The underlying chain is the SAME 2-ary ValueTuple instantiation.
        const auto* under = dynamic_cast<const TS::ParameterizedType*>(tuple->UnderlyingType().get());
        ASSERT_NE(under, nullptr);
        EXPECT_EQ(under->TypeArguments().size(), 2u);
    }
    {
        // S04: the 15-element nesting extracts names a, <null>, c and 12 nulls.
        TS::ITypePtr result = TS::ApplyAttributeTypeVisitor::ApplyAttributesToType(
            outer15, f.compilation, f.Attrs(4), f.synthFile, Tup,
            TS::Nullability::Oblivious);
        const auto* tuple = dynamic_cast<const TS::TupleType*>(result.get());
        ASSERT_NE(tuple, nullptr);
        EXPECT_EQ(tuple->Cardinality(), 15);
        EXPECT_EQ(tuple->ElementNames().size(), 15u);
        EXPECT_EQ(tuple->ElementNames()[0], "a");
        EXPECT_EQ(tuple->ElementNames()[1], "");
        EXPECT_EQ(tuple->ElementNames()[2], "c");
        EXPECT_EQ(tuple->ElementNames()[14], "");
        // The underlying is the rebuilt 8-ary chain (a NEW ParameterizedType
        // tree, not the input): the outer 8-arity over 7 strings + the nested
        // 8-arity over 7 strings + the 1-arity.
        const auto* under = dynamic_cast<const TS::ParameterizedType*>(tuple->UnderlyingType().get());
        ASSERT_NE(under, nullptr);
        EXPECT_EQ(under->TypeArguments().size(), 8u);
        const auto* nested = dynamic_cast<const TS::ParameterizedType*>(under->TypeArguments()[7].get());
        ASSERT_NE(nested, nullptr);
        EXPECT_EQ(nested->TypeArguments().size(), 8u);
    }
}

// ---------------------------------------------------------------------------
// Dynamic: the flag rows and the index walk (S09..S15)
// ---------------------------------------------------------------------------

TEST(ApplyAttributeTypeVisitorTest, DynamicDrives)
{
    AatFixture f;
    auto str = f.KnownType(TS::KnownTypeCode::String);
    auto obj = f.KnownType(TS::KnownTypeCode::Object);
    auto vt2 = f.FindType("System", "ValueTuple", 2);
    auto list = f.FindType("System.Collections.Generic", "List", 1);
    auto vt2StrObj = PT(vt2, { str, obj });
    auto listObj = PT(list, { obj });
    auto modoptObj = std::make_shared<TS::ModifiedType>(str, obj, false);

    const auto Dyn = TS::TypeSystemOptions::Dynamic;
    // fDyn1 = [Dynamic(new[]{false, true})] (row 1), fDyn2 = the empty flags
    // row (row 2), fDyn3 = the MethodDef-ctor row (row 3).
    Drive1("S09", obj, f.compilation, f.synthFile, Dyn, TS::Nullability::Oblivious, f.Attrs(1));
    Drive1("S10", obj, f.compilation, f.synthFile, Dyn, TS::Nullability::Oblivious, f.Attrs(2));
    Drive1("S11", obj, f.compilation, f.synthFile, Dyn, TS::Nullability::Oblivious, f.Attrs(3));
    Drive1("S12", listObj, f.compilation, f.synthFile, Dyn, TS::Nullability::Oblivious, f.Attrs(1));
    Drive1("S13", vt2StrObj, f.compilation, f.synthFile, Dyn, TS::Nullability::Oblivious, f.Attrs(1));
    Drive1("S14", str, f.compilation, f.synthFile, Dyn, TS::Nullability::Oblivious, f.Attrs(1));
    Drive1("S15", modoptObj, f.compilation, f.synthFile, Dyn, TS::Nullability::Oblivious, f.Attrs(1));
}

// ---------------------------------------------------------------------------
// Nullability: the byte and byte[] forms, the array/fnptr/element positions
// (S16..S22)
// ---------------------------------------------------------------------------

TEST(ApplyAttributeTypeVisitorTest, NullabilityDrives)
{
    AatFixture f;
    auto str = f.KnownType(TS::KnownTypeCode::String);
    auto obj = f.KnownType(TS::KnownTypeCode::Object);
    auto vt2 = f.FindType("System", "ValueTuple", 2);
    auto list = f.FindType("System.Collections.Generic", "List", 1);
    auto listStr = PT(list, { str });
    auto vt2StrObj = PT(vt2, { str, obj });
    auto strArray = std::make_shared<TS::ArrayType>(str);
    auto strArray2 = std::make_shared<TS::ArrayType>(str, 2);
    auto fnptr = std::make_shared<TS::FunctionPointerType>(
        TS::SignatureCallingConvention::Default, std::vector<TS::ITypePtr>{},
        obj, false,
        std::vector<TS::ITypePtr>{ obj, obj },
        std::vector<TS::ReferenceKind>{ TS::ReferenceKind::None, TS::ReferenceKind::In });

    const auto NAnn = TS::TypeSystemOptions::NullabilityAnnotations;
    // fNullB1 = [Nullable(1)] (row 5), fNullArr = [Nullable(new[]{0,1,2})]
    // (row 6), fNullB2 = [Nullable(2)] (row 7).
    Drive1("S16", listStr, f.compilation, f.synthFile, NAnn, TS::Nullability::Oblivious, f.Attrs(5));
    Drive1("S17", listStr, f.compilation, f.synthFile, NAnn, TS::Nullability::Oblivious, f.Attrs(6));
    Drive1("S18", listStr, f.compilation, f.synthFile, NAnn, TS::Nullability::NotNullable, std::nullopt);
    Drive1("S19", strArray, f.compilation, f.synthFile, NAnn, TS::Nullability::Oblivious, f.Attrs(6));
    Drive1("S20", strArray2, f.compilation, f.synthFile, NAnn, TS::Nullability::NotNullable, f.Attrs(5));
    Drive1("S21", vt2StrObj, f.compilation, f.synthFile, NAnn, TS::Nullability::Oblivious, f.Attrs(6));
    Drive1("S22", fnptr, f.compilation, f.synthFile, NAnn, TS::Nullability::Oblivious, f.Attrs(6));
}

// ---------------------------------------------------------------------------
// Native integers: the attribute row and the WithoutAttribute flag (S23..S27)
// ---------------------------------------------------------------------------

TEST(ApplyAttributeTypeVisitorTest, NativeIntegerDrives)
{
    AatFixture f;
    auto str = f.KnownType(TS::KnownTypeCode::String);
    auto intptr = f.KnownType(TS::KnownTypeCode::IntPtr);
    auto uintptr = f.KnownType(TS::KnownTypeCode::UIntPtr);

    const auto NatI = TS::TypeSystemOptions::NativeIntegers;
    const auto NatIF = TS::TypeSystemOptions::NativeIntegersWithoutAttribute;
    // fNatInt = the [NativeInteger(new[]{true})] row (row 8).
    Drive1("S23", intptr, f.compilation, f.synthFile, NatI, TS::Nullability::Oblivious, f.Attrs(8));
    Drive1("S24", uintptr, f.compilation, f.synthFile, NatI, TS::Nullability::Oblivious, f.Attrs(8));
    Drive1("S25", intptr, f.compilation, f.synthFile, NatI, TS::Nullability::Oblivious, std::nullopt);
    Drive1("S26", intptr, f.compilation, f.synthFile, NatI | NatIF, TS::Nullability::Oblivious, std::nullopt);
    Drive1("S27", str, f.compilation, f.synthFile, NatI, TS::Nullability::Oblivious, f.Attrs(8));
}

// ---------------------------------------------------------------------------
// KeepModifiers, typeChildrenOnly, the no-options short-circuit, the
// accumulation drive, the additionalAttributes override (S28..S35)
// ---------------------------------------------------------------------------

TEST(ApplyAttributeTypeVisitorTest, ModifierAndCompositionDrives)
{
    AatFixture f;
    auto str = f.KnownType(TS::KnownTypeCode::String);
    auto obj = f.KnownType(TS::KnownTypeCode::Object);
    auto vt2 = f.FindType("System", "ValueTuple", 2);
    auto list = f.FindType("System.Collections.Generic", "List", 1);
    auto listStr = PT(list, { str });
    auto vt2StrObj = PT(vt2, { str, obj });
    auto modoptObj = std::make_shared<TS::ModifiedType>(str, obj, false);
    auto modreqObj = std::make_shared<TS::ModifiedType>(str, obj, true);

    const auto Dyn = TS::TypeSystemOptions::Dynamic;
    const auto NAnn = TS::TypeSystemOptions::NullabilityAnnotations;
    const auto Keep = TS::TypeSystemOptions::KeepModifiers;

    Drive1("S28", modoptObj, f.compilation, f.synthFile, Keep | Dyn, TS::Nullability::Oblivious, f.Attrs(1));
    Drive1("S29", modreqObj, f.compilation, f.synthFile, Keep, TS::Nullability::Oblivious, f.Attrs(7));
    Drive1("S30", listStr, f.compilation, f.synthFile, TS::TypeSystemOptions::Default,
           TS::Nullability::Oblivious, f.Attrs(6), /*childrenOnly=*/true);
    Drive1("S31", vt2StrObj, f.compilation, f.synthFile, TS::TypeSystemOptions::Default,
           TS::Nullability::Oblivious, f.Attrs(4), /*childrenOnly=*/true);
    Drive1("S32", obj, f.compilation, f.synthFile, TS::TypeSystemOptions::None,
           TS::Nullability::Oblivious, f.Attrs(10));
    // fAll (row 10) carries every attribute shape in order -- the later rows
    // override the earlier ones (the accumulation drive).
    Drive1("S33", vt2StrObj, f.compilation, f.synthFile, TS::TypeSystemOptions::Default,
           TS::Nullability::Oblivious, f.Attrs(10));
    Drive1("S34", listStr, f.compilation, f.synthFile, TS::TypeSystemOptions::Default,
           TS::Nullability::Oblivious, f.Attrs(10));
    // The additionalAttributes loop runs AFTER the normal one and overrides
    // the recorded values (fAll's state then fNullB1's [Nullable(1)] default).
    Drive1("S35", listStr, f.compilation, f.synthFile, NAnn, TS::Nullability::Oblivious,
           f.Attrs(10), /*childrenOnly=*/false, AttrsOpt{ f.Attrs(5) });
}

// ---------------------------------------------------------------------------
// The PDB entry: the pre-decoded DynamicFlags / TupleElementNames (P01..P06)
// ---------------------------------------------------------------------------

TEST(ApplyAttributeTypeVisitorTest, PdbEntryDrives)
{
    AatFixture f;
    auto obj = f.KnownType(TS::KnownTypeCode::Object);
    auto str = f.KnownType(TS::KnownTypeCode::String);
    auto vt2 = f.FindType("System", "ValueTuple", 2);
    auto list = f.FindType("System.Collections.Generic", "List", 1);
    auto vt2StrObj = PT(vt2, { str, obj });
    auto listObj = PT(list, { obj });

    ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo pdbEmpty;
    ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo pdbFlags;
    pdbFlags.DynamicFlags = std::vector<bool>{ false, true };
    ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo pdbFlags1;
    pdbFlags1.DynamicFlags = std::vector<bool>{ true };
    ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo pdbNames;
    pdbNames.TupleElementNames = std::vector<std::string>{ "a", "" };
    ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo pdbBoth;
    pdbBoth.DynamicFlags = std::vector<bool>{ true };
    pdbBoth.TupleElementNames = std::vector<std::string>{ "a", "b" };

    Drive2("P01", obj, f.compilation, TS::TypeSystemOptions::Default, pdbEmpty);
    Drive2("P02", obj, f.compilation, TS::TypeSystemOptions::Default, pdbFlags);
    Drive2("P03", listObj, f.compilation, TS::TypeSystemOptions::Default, pdbFlags);
    Drive2("P04", vt2StrObj, f.compilation, TS::TypeSystemOptions::Default, pdbNames);
    Drive2("P05", vt2StrObj, f.compilation, TS::TypeSystemOptions::Default, pdbBoth);
    Drive2("P06", obj, f.compilation, TS::TypeSystemOptions::None, pdbFlags1);
}
