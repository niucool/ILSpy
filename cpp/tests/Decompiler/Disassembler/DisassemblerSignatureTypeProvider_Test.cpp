// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Tests for the DisassemblerSignatureTypeProvider port
// (cpp/Decompiler/Disassembler/DisassemblerSignatureTypeProvider.{hpp,cpp}), the
// C++-only SRM gap fill it instantiates (cpp/Decompiler/Metadata/
// SignatureTypeProvider.{hpp,cpp} -- the ISignatureTypeProvider contract +
// SignatureDecoder-style blob walker), and the minimal EntityHandle.WriteTo
// family (cpp/Decompiler/IL/InstructionOutputExtensions.{hpp,cpp}) the
// provider's TypeDef/TypeRef arms and the ExceptionRegion catch-type writer
// consume. The real-assembly fixtures are mscorlib (self-contained types) and
// the .NET Framework System.dll (whose TypeRef rows carry AssemblyRef scopes,
// the `[mscorlib]` prefix fixture). Expected spellings follow the C# bodies
// verbatim (the ILDasm IL text).

#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler;
using Disassembler::DisassemblerSignatureTypeProvider;
using Disassembler::ILNameSyntax;
using Metadata::ArrayShape;
using Metadata::MetadataFile;
using Metadata::MetadataGenericContext;
using Metadata::MethodSignatureT;
using Metadata::PrimitiveTypeCode;
using Metadata::SignatureCallingConvention;
using Metadata::SignatureHeader;
using Metadata::SignatureTypeWriter;
using Output::PlainTextOutput;

namespace {

#if defined(_WIN32)
const char* MscorlibPath() { return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll"; }
const char* SystemDllPath() {
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
           "v4.0_4.0.0.0__b77a5c561934e089\\System.dll";
}
#else
const char* MscorlibPath() { return "/usr/lib/mono/4.5/mscorlib.dll"; }
const char* SystemDllPath() { return "/usr/lib/mono/4.5/System.dll"; }
#endif

// Render through the real writer stack (PlainTextOutput over a stringstream).
std::string Render(const std::function<void(Output::ITextOutput&)>& body) {
    std::ostringstream stream;
    PlainTextOutput output(stream);
    body(output);
    return stream.str();
}

std::string RenderWriter(const MetadataFile& module, const SignatureTypeWriter& writer) {
    return Render([&](Output::ITextOutput& out) {
        DisassemblerSignatureTypeProvider provider(module, out);
        // The C# GetPrimitiveType et al. return writers that ignore the
        // syntax; invoke through the provider-consistent entry.
        writer(ILNameSyntax::Signature);
    });
}

// A provider-bound writer built from a unit provider callback (mirrors how
// the C# tests invoke individual Get* methods).
SignatureTypeWriter BindProvider(const MetadataFile& module, Output::ITextOutput& out,
    const std::function<SignatureTypeWriter(DisassemblerSignatureTypeProvider&)>& call) {
    DisassemblerSignatureTypeProvider provider(module, out);
    return call(provider);
}

// A pre-rendered provider callback (the single-provider pattern for Get*
// method assertions): build the provider over a module, invoke the Get*
// member, render the returned writer.
std::string RenderProviderCall(const MetadataFile& module,
    const std::function<SignatureTypeWriter(DisassemblerSignatureTypeProvider&)>& call) {
    std::ostringstream stream;
    PlainTextOutput output(stream);
    DisassemblerSignatureTypeProvider provider(module, output);
    SignatureTypeWriter writer = call(provider);
    writer(ILNameSyntax::Signature);
    return stream.str();
}

// Find a TypeDef row by (namespace, name); 0 when absent.
std::uint32_t FindTypeDefToken(const MetadataFile& f, std::string_view ns, std::string_view name) {
    std::uint32_t count = f.TypeDefCount();
    for (std::uint32_t row = 1; row <= count; row++) {
        std::uint32_t token = (0x02u << 24) | row;
        auto info = f.GetTypeDefNameInfo(token);
        if (info && info->Name == name && info->Namespace == ns)
            return token;
    }
    return 0;
}

// Find a TypeRef row by (namespace, name); 0 when absent.
std::uint32_t FindTypeRefToken(const MetadataFile& f, std::string_view ns, std::string_view name) {
    std::uint32_t count = f.TypeRefCount();
    for (std::uint32_t row = 1; row <= count; row++) {
        std::uint32_t token = (0x01u << 24) | row;
        auto info = f.GetTypeRefNameInfo(token);
        if (info && info->Name == name && info->Namespace == ns)
            return token;
    }
    return 0;
}

// Find the TypeSpec row whose WriteTo rendering equals `expected` (the
// data-driven fixture for a real generic instantiation); 0 when absent.
std::uint32_t FindTypeSpecByRendering(const MetadataFile& f, std::string_view expected) {
    // The TypeSpec table's row count via a scan bound: GetTypeSpecSignatureBlob
    // rejects out-of-range rows, so scan to the first nullopt row.
    for (std::uint32_t row = 1;; row++) {
        std::uint32_t token = (0x1Bu << 24) | row;
        if (!f.GetTypeSpecSignatureBlob(token))
            break;
        std::string rendered = Render([&](Output::ITextOutput& out) {
            IL::WriteTo(f, out, MetadataGenericContext{}, token);
        });
        if (rendered == expected)
            return token;
    }
    return 0;
}

// The mscorlib TypeSpec fixture's ILAsm rendering: a GENERICINST of the
// IEnumerable`1 TypeDef -- the IL name keeps the arity suffix (the
// FullTypeName reflection form), and an unnamed generic context renders the
// type parameter positionally (`!0`).
constexpr const char* kEnumerableSpecRendering =
    "class System.Collections.Generic.IEnumerable`1<!0>";

// The ECMA-335 II.23.2 compressed-unsigned encoding (the 1/2/4-byte forms),
// for building synthetic signature blobs in the tests.
std::vector<std::uint8_t> CompressedUint(std::uint32_t v) {
    if (v < 0x80) return {static_cast<std::uint8_t>(v)};
    if (v < 0x4000)
        return {static_cast<std::uint8_t>((v >> 8) | 0x80),
                static_cast<std::uint8_t>(v & 0xFF)};
    return {static_cast<std::uint8_t>((v >> 24) | 0xC0),
            static_cast<std::uint8_t>((v >> 16) & 0xFF),
            static_cast<std::uint8_t>((v >> 8) & 0xFF),
            static_cast<std::uint8_t>(v & 0xFF)};
}

} // namespace

// ---------------------------------------------------------------------------
// The provider's primitive spellings (the C# GetPrimitiveType switch).
// ---------------------------------------------------------------------------

TEST(DisassemblerSignatureTypeProviderTest, PrimitiveSpellingsMatchIlDasm) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::SByte); }), "int8");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Int16); }), "int16");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Int32); }), "int32");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Int64); }), "int64");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Byte); }), "uint8");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::UInt16); }), "uint16");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::UInt32); }), "uint32");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::UInt64); }), "uint64");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Single); }), "float32");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Double); }), "float64");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Void); }), "void");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Boolean); }), "bool");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::String); }), "string");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Char); }), "char");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::Object); }), "object");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::IntPtr); }), "native int");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::UIntPtr); }), "native uint");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) { return p.GetPrimitiveType(PrimitiveTypeCode::TypedReference); }), "typedref");
}

TEST(DisassemblerSignatureTypeProviderTest, UnknownPrimitiveThrows) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // A code outside the C# switch (the port's enum carries no such value, so
    // cast a fake byte through the underlying type).
    auto bogus = static_cast<PrimitiveTypeCode>(0xEE);
    EXPECT_THROW((void)RenderProviderCall(f, [&](auto& p) { return p.GetPrimitiveType(bogus); }),
        std::out_of_range);
}

// ---------------------------------------------------------------------------
// Wrapper writers (SZArray / Ptr / ByRef / Pinned / Modified).
// ---------------------------------------------------------------------------

TEST(DisassemblerSignatureTypeProviderTest, SzArrayWrapsElementWithBrackets) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) {
                  return p.GetSZArrayType(p.GetPrimitiveType(PrimitiveTypeCode::Int32));
              }),
        "int32[]");
}

TEST(DisassemblerSignatureTypeProviderTest, PointerAndByRefAppendTheirTokens) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) {
                  return p.GetPointerType(p.GetPrimitiveType(PrimitiveTypeCode::Int32));
              }),
        "int32*");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) {
                  return p.GetByReferenceType(p.GetPrimitiveType(PrimitiveTypeCode::Int32));
              }),
        "int32&");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) {
                  return p.GetPinnedType(p.GetPrimitiveType(PrimitiveTypeCode::Int32));
              }),
        "int32 pinned");
}

TEST(DisassemblerSignatureTypeProviderTest, ModifiedTypeRendersModreqModopt) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) {
                  return p.GetModifiedType(p.GetPrimitiveType(PrimitiveTypeCode::Int32),
                      p.GetPrimitiveType(PrimitiveTypeCode::Boolean), true);
              }),
        "bool modreq(int32)");
    EXPECT_EQ(RenderProviderCall(f, [](auto& p) {
                  return p.GetModifiedType(p.GetPrimitiveType(PrimitiveTypeCode::Int32),
                      p.GetPrimitiveType(PrimitiveTypeCode::Boolean), false);
              }),
        "bool modopt(int32)");
}

TEST(DisassemblerSignatureTypeProviderTest, ArrayShapeRendersBoundsAndSizes) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // rank 2, sizes {3,4}, no lower bounds -> the C# writes the lower bound
    // ONLY when the row has one, so the rendering is "[...2, ...3]".
    ArrayShape shape;
    shape.Rank = 2;
    shape.Sizes = {3, 4};
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetArrayType(p.GetPrimitiveType(PrimitiveTypeCode::Int32), shape);
              }),
        "int32[...2, ...3]");
    // rank 2 with explicit zero lower bounds and sizes {3,4} -> "[0...2, 0...3]".
    ArrayShape bounded;
    bounded.Rank = 2;
    bounded.Sizes = {3, 4};
    bounded.LowerBounds = {0, 0};
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetArrayType(p.GetPrimitiveType(PrimitiveTypeCode::Int32), bounded);
              }),
        "int32[0...2, 0...3]");
    // rank 1, lower bound -1, size 5 -> "[-1...3]".
    ArrayShape offset;
    offset.Rank = 1;
    offset.LowerBounds = {-1};
    offset.Sizes = {5};
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetArrayType(p.GetPrimitiveType(PrimitiveTypeCode::Int32), offset);
              }),
        "int32[-1...3]");
}

TEST(DisassemblerSignatureTypeProviderTest, FunctionPointerRendersMethodHeader) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    MethodSignatureT sig;
    sig.Header = SignatureHeader::Decode(0x00);  // default convention, no this
    sig.ReturnType = [](ILNameSyntax) {};
    // The unit test drives the writer through a provider-rendered FnPtr only
    // for the header text; the real FnPtr assembly needs a provider-bound
    // writer, so assert via GetFunctionPointerType over a signature whose
    // return type is int32 and params are (int32, int32).
    std::ostringstream stream;
    PlainTextOutput output(stream);
    DisassemblerSignatureTypeProvider provider(f, output);
    Metadata::SignatureTypeWriter int32 =
        provider.GetPrimitiveType(PrimitiveTypeCode::Int32);
    sig.ReturnType = int32;
    sig.ParameterTypes = {int32, int32};
    provider.GetFunctionPointerType(sig)(ILNameSyntax::Signature);
    // The bare calling convention (0x00) writes no header prefix; a hasThis
    // FnPtr sig would render "method instance int32 *(...)" (the header
    // writer's IsInstance arm).
    EXPECT_EQ(stream.str(), "method int32 *(int32, int32)");
}

// ---------------------------------------------------------------------------
// Type-definition / reference arms over real mscorlib rows.
// ---------------------------------------------------------------------------

TEST(DisassemblerSignatureTypeProviderTest, GetTypeFromDefinitionRendersFullTypeName) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t objectToken = FindTypeDefToken(f, "System", "Object");
    ASSERT_NE(objectToken, 0u);
    // rawTypeKind 0x12 -> "class " prefix (the C# switch).
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetTypeFromDefinition(objectToken, 0x12);
              }),
        "class System.Object");
    // rawTypeKind 0x00 -> no prefix.
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetTypeFromDefinition(objectToken, 0x00);
              }),
        "System.Object");
}

TEST(DisassemblerSignatureTypeProviderTest, GetTypeFromDefinitionRejectsUnknownRawTypeKind) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t objectToken = FindTypeDefToken(f, "System", "Object");
    ASSERT_NE(objectToken, 0u);
    EXPECT_THROW((void)RenderProviderCall(f, [&](auto& p) {
                     return p.GetTypeFromDefinition(objectToken, 0x99);
                 }),
        std::logic_error);
}

TEST(DisassemblerSignatureTypeProviderTest, GetTypeFromReferenceRendersScopePrefix) {
    MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    // System.dll's TypeRef to System.String resolves into the mscorlib
    // assembly, so the ILAsm rendering carries the [mscorlib] prefix.
    std::uint32_t stringToken = FindTypeRefToken(f, "System", "String");
    ASSERT_NE(stringToken, 0u);
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetTypeFromReference(stringToken, 0x12);
              }),
        "class [mscorlib]System.String");
}

// ---------------------------------------------------------------------------
// Generic type/method parameters over real mscorlib GenericParam rows.
// ---------------------------------------------------------------------------

TEST(DisassemblerSignatureTypeProviderTest, GenericTypeParameterUsesRowNameWhenNamed) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t listToken = FindTypeDefToken(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listToken, 0u);
    MetadataGenericContext context = MetadataGenericContext::ForType(listToken, f);
    // The C# "!T" form (the named parameter) at the default syntax.
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetGenericTypeParameter(context, 0);
              }),
        "!T");
    // The C# "!0" form at the SignatureNoNamedTypeParameters syntax (the
    // WriteTypeParameter bare-index arm).
    std::ostringstream stream;
    PlainTextOutput output(stream);
    DisassemblerSignatureTypeProvider provider(f, output);
    provider.GetGenericTypeParameter(context, 0)(ILNameSyntax::SignatureNoNamedTypeParameters);
    EXPECT_EQ(stream.str(), "!0");
}

TEST(DisassemblerSignatureTypeProviderTest, GenericMethodParameterUsesDoubleBang) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // Array.Sort<T>(T[]) carries a method generic parameter named T.
    std::uint32_t arrayToken = FindTypeDefToken(f, "System", "Array");
    ASSERT_NE(arrayToken, 0u);
    // Find a method on Array with MVAR rows: scan methods for one with
    // GetGenericParameters non-empty (the provider path is context-driven,
    // so any such method works).
    std::uint32_t found = 0;
    for (const auto& m : f.GetMethods(arrayToken)) {
        auto gps = f.GetGenericParameters(m.Token);
        if (!gps.empty()) {
            found = m.Token;
            break;
        }
    }
    ASSERT_NE(found, 0u) << "System.Array must carry at least one generic method";
    MetadataGenericContext context = MetadataGenericContext::ForMethod(found, f);
    EXPECT_EQ(RenderProviderCall(f, [&](auto& p) {
                  return p.GetGenericMethodParameter(context, 0);
              }),
        "!!T");
}

// ---------------------------------------------------------------------------
// EntityHandle.WriteTo (IL/InstructionOutputExtensions).
// ---------------------------------------------------------------------------

TEST(InstructionOutputExtensionsTest, NilTokenRendersNilMarker) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_EQ(Render([&](Output::ITextOutput& out) {
                  IL::WriteTo(f, out, MetadataGenericContext{}, 0);
              }),
        "<nil>");
}

TEST(InstructionOutputExtensionsTest, TypeDefinitionArmRendersName) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t objectToken = FindTypeDefToken(f, "System", "Object");
    ASSERT_NE(objectToken, 0u);
    EXPECT_EQ(Render([&](Output::ITextOutput& out) {
                  IL::WriteTo(f, out, MetadataGenericContext{}, objectToken);
              }),
        "System.Object");
}

TEST(InstructionOutputExtensionsTest, TypeReferenceArmRendersAssemblyPrefix) {
    MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringToken = FindTypeRefToken(f, "System", "String");
    ASSERT_NE(stringToken, 0u);
    EXPECT_EQ(Render([&](Output::ITextOutput& out) {
                  IL::WriteTo(f, out, MetadataGenericContext{}, stringToken);
              }),
        "[mscorlib]System.String");
}

TEST(InstructionOutputExtensionsTest, TypeSpecificationArmDecodesThroughProvider) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // The classic generic-interface instantiation TypeSpec: mscorlib carries
    // a `System.Collections.Generic.IEnumerable`1<!0>` GENERICINST (the ILDasm
    // spelling for an unnamed generic context). The scan finds the row by
    // rendering, so the fixture is independent of table-row numbering.
    std::uint32_t token = FindTypeSpecByRendering(f, kEnumerableSpecRendering);
    ASSERT_NE(token, 0u)
        << "mscorlib must carry the IEnumerable`1<!0> TypeSpec";
    EXPECT_EQ(Render([&](Output::ITextOutput& out) {
                  IL::WriteTo(f, out, MetadataGenericContext{}, token);
              }),
        kEnumerableSpecRendering);
}

TEST(InstructionOutputExtensionsTest, TypeSpecificationArmRendersNamedTypeParameter) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // The same TypeSpec decoded under a named generic context (List`1) names
    // the type parameter from its GenericParam row.
    std::uint32_t enumerableToken = FindTypeSpecByRendering(f, kEnumerableSpecRendering);
    ASSERT_NE(enumerableToken, 0u);
    std::uint32_t listToken = FindTypeDefToken(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listToken, 0u);
    MetadataGenericContext context = MetadataGenericContext::ForType(listToken, f);
    EXPECT_EQ(Render([&](Output::ITextOutput& out) {
                  IL::WriteTo(f, out, context, enumerableToken);
              }),
        "class System.Collections.Generic.IEnumerable`1<!T>");
}

TEST(InstructionOutputExtensionsTest, UnknownHandleKindRendersAtToken) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // A member-table token (MethodDef) hits the C# default arm in the port
    // (the member arms are deferred with the MethodBodyDisassembler region).
    std::uint32_t objectToken = FindTypeDefToken(f, "System", "Object");
    ASSERT_NE(objectToken, 0u);
    auto methods = f.GetMethods(objectToken);
    ASSERT_FALSE(methods.empty());
    std::uint32_t methodToken = methods.front().Token;
    EXPECT_EQ(Render([&](Output::ITextOutput& out) {
                  IL::WriteTo(f, out, MetadataGenericContext{}, methodToken);
              }),
        "@" + [&] {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(methodToken));
            return std::string(buf);
        }());
}

// ---------------------------------------------------------------------------
// The SignatureHeader writer + WriteParameterList + ToILSyntax.
// ---------------------------------------------------------------------------

TEST(InstructionOutputExtensionsTest, SignatureHeaderPrefixes) {
    EXPECT_EQ(Render([](Output::ITextOutput& out) {
                  IL::WriteTo(SignatureHeader::Decode(0x60), out);  // explicit this
              }),
        "instance explicit ");
    EXPECT_EQ(Render([](Output::ITextOutput& out) {
                  IL::WriteTo(SignatureHeader::Decode(0x20), out);  // has this
              }),
        "instance ");
    EXPECT_EQ(Render([](Output::ITextOutput& out) {
                  IL::WriteTo(SignatureHeader::Decode(0x05), out);  // vararg
              }),
        "vararg ");
    EXPECT_EQ(Render([](Output::ITextOutput& out) {
                  IL::WriteTo(SignatureHeader::Decode(0x00), out);  // default
              }),
        "");
}

TEST(SRMExtensionsToILSyntaxTest, ConventionSpellings) {
    EXPECT_EQ(Metadata::ToILSyntax(SignatureCallingConvention::Default), "default");
    EXPECT_EQ(Metadata::ToILSyntax(SignatureCallingConvention::CDecl), "unmanaged cdecl");
    EXPECT_EQ(Metadata::ToILSyntax(SignatureCallingConvention::StdCall), "unmanaged stdcall");
    EXPECT_EQ(Metadata::ToILSyntax(SignatureCallingConvention::ThisCall), "unmanaged thiscall");
    EXPECT_EQ(Metadata::ToILSyntax(SignatureCallingConvention::FastCall), "unmanaged fastcall");
    EXPECT_EQ(Metadata::ToILSyntax(SignatureCallingConvention::VarArgs), "vararg");
    EXPECT_EQ(Metadata::ToILSyntax(SignatureCallingConvention::Unmanaged), "unmanaged");
}

// ---------------------------------------------------------------------------
// The provider-driven blob walker (SignatureTypeProviderDecoder) end-to-end.
// ---------------------------------------------------------------------------

TEST(SignatureTypeProviderDecoderTest, SzArrayOfInt32Blob) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // ELEMENT_TYPE_SZARRAY (0x1D) of ELEMENT_TYPE_I4 (0x08).
    const std::uint8_t blob[] = {0x1D, 0x08};
    std::ostringstream stream;
    PlainTextOutput output(stream);
    DisassemblerSignatureTypeProvider provider(f, output);
    Metadata::SignatureTypeProviderDecoder decoder(provider, f);
    decoder.DecodeType(blob, sizeof(blob), MetadataGenericContext{})(ILNameSyntax::Signature);
    EXPECT_EQ(stream.str(), "int32[]");
}

TEST(SignatureTypeProviderDecoderTest, GenericInstantiationBlobThroughWriteTo) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t listToken = FindTypeDefToken(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listToken, 0u);
    // ELEMENT_TYPE_GENERICINST (0x15), CLASS (0x12), List`1 TypeDef coded
    // index (tag 0), one argument, ELEMENT_TYPE_I4. (mscorlib references its
    // own types by TypeDef, so there is no List`1 TypeRef to use here.)
    std::uint32_t coded = ((listToken & 0x00FFFFFFu) << 2) | 0u;  // TypeDef tag
    std::vector<std::uint8_t> blob = {0x15, 0x12};
    for (std::uint8_t b : CompressedUint(coded)) blob.push_back(b);
    blob.push_back(0x01);  // one type argument
    blob.push_back(0x08);  // ELEMENT_TYPE_I4
    std::ostringstream stream;
    PlainTextOutput output(stream);
    DisassemblerSignatureTypeProvider provider(f, output);
    Metadata::SignatureTypeProviderDecoder decoder(provider, f);
    decoder.DecodeType(blob.data(), blob.size(), MetadataGenericContext{})(ILNameSyntax::Signature);
    EXPECT_EQ(stream.str(), "class System.Collections.Generic.List`1<int32>");
}

TEST(SignatureTypeProviderDecoderTest, NestedTypeSpecRecursesThroughModuleRead) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // A TypeSpec of a TypeSpec: SZArray (0x1D) wrapping a TypeDefOrRefEncoded
    // tag-2 row -- the inner TypeSpec's blob decodes through
    // GetTypeFromSpecification (the pimpl-constrained module read).
    std::uint32_t innerToken = FindTypeSpecByRendering(f, kEnumerableSpecRendering);
    ASSERT_NE(innerToken, 0u);
    std::uint32_t innerRow = innerToken & 0x00FFFFFFu;
    if (innerRow <= 0x3F) {
        // single-byte compressed form; a TypeSpec element is CLASS-marked
        // (ELEMENT_TYPE_CLASS + the tag-2 coded index), per II.23.2.12.
        std::vector<std::uint8_t> blob = {0x1D, 0x12};
        for (std::uint8_t b : CompressedUint((innerRow << 2) | 2u)) blob.push_back(b);
        std::ostringstream stream;
        PlainTextOutput output(stream);
        DisassemblerSignatureTypeProvider provider(f, output);
        Metadata::SignatureTypeProviderDecoder decoder(provider, f);
        decoder.DecodeType(blob.data(), blob.size(), MetadataGenericContext{})(ILNameSyntax::Signature);
        EXPECT_EQ(stream.str(), "class System.Collections.Generic.IEnumerable`1<!0>[]");
    } else {
        ADD_FAILURE() << "the fixture TypeSpec row exceeded the single-byte coded form";
    }
}

TEST(SignatureTypeProviderDecoderTest, MethodSignatureBlobDecodesVarargShape) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // A method signature: DEFAULT|HASTHIS (0x20), 2 params, int32 return,
    // (int32, int32) params.
    const std::uint8_t blob[] = {0x20, 0x02, 0x08, 0x08, 0x08};
    std::ostringstream stream;
    PlainTextOutput output(stream);
    DisassemblerSignatureTypeProvider provider(f, output);
    Metadata::SignatureTypeProviderDecoder decoder(provider, f);
    MethodSignatureT sig = decoder.DecodeMethodSignature(blob, sizeof(blob), MetadataGenericContext{});
    EXPECT_EQ(sig.ParameterTypes.size(), 2u);
    EXPECT_EQ(sig.RequiredParameterCount, 2u);
    // The header is instance (hasThis, not explicit).
    EXPECT_TRUE(sig.Header.IsInstance());
    // Render via WriteParameterList (the C# "(int32, int32)" shape).
    stream.str("");
    stream.clear();
    PlainTextOutput output2(stream);
    IL::WriteParameterList(output2, sig);
    EXPECT_EQ(stream.str(), "(int32, int32)");
}

TEST(SignatureTypeProviderDecoderTest, TruncatedBlobThrows) {
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // SZArray marker without the element type.
    const std::uint8_t blob[] = {0x1D};
    std::ostringstream stream;
    PlainTextOutput output(stream);
    DisassemblerSignatureTypeProvider provider(f, output);
    Metadata::SignatureTypeProviderDecoder decoder(provider, f);
    EXPECT_THROW((void)decoder.DecodeType(blob, sizeof(blob), MetadataGenericContext{}),
        std::logic_error);
}
