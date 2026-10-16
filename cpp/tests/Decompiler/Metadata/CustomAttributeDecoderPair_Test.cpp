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

// Tests for the CustomAttributeDecoderT<TProvider> template instantiation
// over a NON-IType provider (the ReflectionDisassembler
// SecurityDeclarationDecoder shape: TType = a (PrimitiveTypeCode Code,
// string Name) pair). The decoder template is the port's lift of the old
// convention-(a) absorption -- both C# decoders are generic over TType, and
// the disassembler instantiates the repo copy with the pair tuple -- so the
// genericity is pinned by driving the SAME fixtures the IType instantiation
// is pinned over (the synthetic manifest's attribute rows) plus the
// standalone named-argument bytes, through a test-local pair provider:
//   * DecodeValue over the manifest rows whose fixed-argument shapes cover
//     the primitive / SZArray / Type-handle / string / null arms and the
//     MethodDef/MemberRef constructor dispatch;
//   * DecodeNamedArguments over the standalone bytes (the five named args,
//     the security-declaration shape TryDecodeSecurityDeclaration drives);
//   * the provideBoxingTypeInfo=true boxing arm (the TaggedObject wrapper).
// The pair provider's GetTypeFromDefinition/GetTypeFromReference stubs
// return { 0, "type" } for every handle (mirroring GetSystemType's pair), so
// the TypeCode machinery (IsSystemType -> SerializationTypeCode::Type) is
// exercised through the stub the same way the real provider drives it.

#include <cstdlib>
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "TestFixtures/CadDecoderGold.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;

// The C# tuple's zero code (the pair for a type that carries no underlying
// primitive -- the C# `(PrimitiveTypeCode Code, string Name)` initializes
// Item1 with 0, which the enum has no member for).
constexpr TM::PrimitiveTypeCode kNoCode = static_cast<TM::PrimitiveTypeCode>(0);

// The C# `(PrimitiveTypeCode Code, string Name)` tuple the disassembler's
// SecurityDeclarationDecoder instantiates the decoder with: the underlying
// primitive code (0 for a non-primitive / unresolved type) plus the nullable
// display name (the "enum <name>" / full-name / "type" forms).
struct PairType {
    TM::PrimitiveTypeCode Code = kNoCode;
    std::optional<std::string> Name;

    bool operator==(const PairType& other) const {
        return Code == other.Code && Name == other.Name;
    }
};

// The C# PrimitiveTypeCodeToString spellings (the test-local copy the
// pair's null-Name primitives render through, the same names
// PairProvider.PrimitiveName builds the SZArray names from).
std::string PrimitiveName(TM::PrimitiveTypeCode code) {
    switch (code) {
        case TM::PrimitiveTypeCode::Boolean: return "bool";
        case TM::PrimitiveTypeCode::Byte: return "uint8";
        case TM::PrimitiveTypeCode::SByte: return "int8";
        case TM::PrimitiveTypeCode::Char: return "char";
        case TM::PrimitiveTypeCode::Int16: return "int16";
        case TM::PrimitiveTypeCode::UInt16: return "uint16";
        case TM::PrimitiveTypeCode::Int32: return "int32";
        case TM::PrimitiveTypeCode::UInt32: return "uint32";
        case TM::PrimitiveTypeCode::Int64: return "int64";
        case TM::PrimitiveTypeCode::UInt64: return "uint64";
        case TM::PrimitiveTypeCode::Single: return "float32";
        case TM::PrimitiveTypeCode::Double: return "float64";
        case TM::PrimitiveTypeCode::String: return "string";
        case TM::PrimitiveTypeCode::Object: return "object";
        default: return "unknown";
    }
}

// The test provider: the C# ICustomAttributeTypeProvider<(PrimitiveTypeCode,
// string)> contract's members with stub arms. Every TypeDef/TypeRef handle
// reports the System.Type pair (so a Type-typed argument's TypeCode becomes
// SerializationTypeCode::Type, driving the serialized-name value path).
class PairProvider {
public:
    using TType = PairType;

    PairType GetPrimitiveType(TM::PrimitiveTypeCode typeCode) {
        return PairType{typeCode, std::nullopt};
    }

    PairType GetSZArrayType(PairType elementType) {
        return PairType{elementType.Code,
            (elementType.Name ? *elementType.Name
                              : PrimitiveName(elementType.Code)) + "[]"};
    }

    PairType GetTypeFromDefinition(const TM::MetadataFile&,
        std::uint32_t, std::uint8_t) {
        return PairType{kNoCode, std::string("type")};
    }

    PairType GetTypeFromReference(const TM::MetadataFile&,
        std::uint32_t, std::uint8_t) {
        return PairType{kNoCode, std::string("type")};
    }

    PairType GetTypeFromSerializedName(const std::string& name) {
        return PairType{kNoCode, name};
    }

    PairType GetSystemType() {
        return PairType{kNoCode, std::string("type")};
    }

    TM::PrimitiveTypeCode GetUnderlyingEnumType(PairType type) {
        return type.Code;
    }

    bool IsSystemType(PairType type) {
        return type.Name == std::optional<std::string>("type");
    }

};

// Writes the embedded manifest to a temp file (MetadataFile needs a real
// file) -- the AssemblyIdentityFixtures WriteTempAssembly convention.
std::string WriteSynthManifest() {
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "CadSynthPair_test.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(ILSpy::Tests::kCadSynthManifest, 1,
        ILSpy::Tests::kCadSynthManifestSize, out);
    std::fclose(out);
    return path.string();
}

std::string PairStr(const PairType& pair) {
    if (!pair.Name) return PrimitiveName(pair.Code);
    return *pair.Name;
}

// The value-shape renderer over the pair instantiation's std::any payloads.
std::string ShapeOf(const std::any& v) {
    using TypedArg = TS::CustomAttributeTypedArgumentT<PairType>;
    if (!v.has_value()) return "null";
    if (auto b = std::any_cast<bool>(&v))
        return *b ? "bool:True" : "bool:False";
    if (auto s = std::any_cast<std::string>(&v))
        return "str:\"" + *s + "\"";
    if (auto i32 = std::any_cast<std::int32_t>(&v))
        return "num:Int32:" + std::to_string(*i32);
    if (auto f = std::any_cast<float>(&v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "float:%g", static_cast<double>(*f));
        return buf;
    }
    if (auto pair = std::any_cast<PairType>(&v))
        return "type:<" + PairStr(*pair) + ">";
    if (auto arr = std::any_cast<std::vector<TypedArg>>(&v)) {
        std::string out = "arr:n=" + std::to_string(arr->size()) + ":[";
        for (std::size_t i = 0; i < arr->size(); i++) {
            if (i > 0) out += "|";
            out += ShapeOf((*arr)[i].Value());
        }
        out += "]";
        return out;
    }
    if (auto boxed = std::any_cast<TypedArg>(&v))
        return "boxed:type=<" + PairStr(boxed->Type()) + ">:val="
            + ShapeOf(boxed->Value());
    return "other";
}

// Decodes one manifest row through the pair provider and renders every
// argument as "F<i>:type=<pair>:<value-shape>" / "N<i>:name=..:type=<pair>:
// <value-shape>" lines -- the kCadSynthGold row shapes with the pair-typed
// renders.
std::vector<std::string> RenderRow(std::uint32_t row,
    const TM::MetadataFile& manifest, bool boxing) {
    std::vector<std::string> lines;
    auto info = manifest.GetCustomAttribute(0x0C000000 | row);
    EXPECT_TRUE(info.has_value());
    if (!info || !info->ValueBlob)
        return lines;
    PairProvider provider;
    TM::CustomAttributeDecoderT<PairProvider> decoder(manifest, provider,
        boxing);
    try {
        auto value = decoder.DecodeValue(info->ConstructorToken,
            info->ValueBlob->data(), info->ValueBlob->size());
        lines.push_back("ok:F=" + std::to_string(value.FixedArguments.size())
            + ":N=" + std::to_string(value.NamedArguments.size()));
        for (std::size_t i = 0; i < value.FixedArguments.size(); i++) {
            const auto& a = value.FixedArguments[i];
            lines.push_back("F" + std::to_string(i) + ":type=<"
                + PairStr(a.Type()) + ">:" + ShapeOf(a.Value()));
        }
        for (std::size_t i = 0; i < value.NamedArguments.size(); i++) {
            const auto& a = value.NamedArguments[i];
            lines.push_back("N" + std::to_string(i) + ":name=" + a.Name()
                + ":kind=" + (a.Kind()
                        == TS::CustomAttributeNamedArgumentKind::Field
                    ? "Field" : "Property")
                + ":type=<" + PairStr(a.Type()) + ">:" + ShapeOf(a.Value()));
        }
    } catch (const std::exception& ex) {
        lines.push_back(std::string("EXCEPTION:") + ex.what());
    }
    return lines;
}

TEST(CustomAttributeDecoderPairTest, DecodeValueDecodesPrimitiveRows) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());

    // Row 7: one Int32[] fixed argument, null array value.
    auto row7 = RenderRow(7, manifest, false);
    ASSERT_EQ(row7.size(), 2u);
    EXPECT_EQ(row7[0], "ok:F=1:N=0");
    EXPECT_EQ(row7[1], "F0:type=<int32[]>:null");

    // Row 8: the empty array.
    auto row8 = RenderRow(8, manifest, false);
    EXPECT_EQ(row8[1], "F0:type=<int32[]>:arr:n=0:[]");

    // Row 9: [1|2|3].
    auto row9 = RenderRow(9, manifest, false);
    EXPECT_EQ(row9[1],
        "F0:type=<int32[]>:arr:n=3:[num:Int32:1|num:Int32:2|num:Int32:3]");

    // Row 10: no arguments at all.
    auto row10 = RenderRow(10, manifest, false);
    ASSERT_EQ(row10.size(), 1u);
    EXPECT_EQ(row10[0], "ok:F=0:N=0");

    // Row 12: the MemberReference constructor, an Int32 fixed argument.
    auto row12 = RenderRow(12, manifest, false);
    ASSERT_EQ(row12.size(), 2u);
    EXPECT_EQ(row12[0], "ok:F=1:N=0");
    EXPECT_EQ(row12[1], "F0:type=<int32>:num:Int32:77");
}

TEST(CustomAttributeDecoderPairTest, DecodeValueDecodesTypeHandleRows) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());

    // Row 1: Boolean[] / String / Type (a TypeDef handle: the stub reports
    // the System.Type pair, so the value decodes through the serialized
    // name) / String.
    auto row1 = RenderRow(1, manifest, false);
    ASSERT_EQ(row1.size(), 5u);
    EXPECT_EQ(row1[0], "ok:F=4:N=0");
    EXPECT_EQ(row1[1], "F0:type=<bool[]>:arr:n=2:[bool:True|bool:False]");
    EXPECT_EQ(row1[2], "F1:type=<string>:str:\"hi\"");
    EXPECT_EQ(row1[3], "F2:type=<type>:type:<NS.MyEnum>");
    EXPECT_EQ(row1[4], "F3:type=<string>:str:\"tagged\"");

    // Row 3: the empty string, the generic Type handle, the Int32 42.
    auto row3 = RenderRow(3, manifest, false);
    ASSERT_EQ(row3.size(), 5u);
    EXPECT_EQ(row3[1], "F0:type=<bool[]>:arr:n=0:[]");
    EXPECT_EQ(row3[2], "F1:type=<string>:str:\"\"");
    EXPECT_EQ(row3[3], "F2:type=<type>:type:<NS.GAttr`1>");
    EXPECT_EQ(row3[4], "F3:type=<int32>:num:Int32:42");

    // Row 4: a null string fixed argument and the System.String Type value.
    auto row4 = RenderRow(4, manifest, false);
    ASSERT_EQ(row4.size(), 5u);
    EXPECT_EQ(row4[1], "F0:type=<bool[]>:arr:n=1:[bool:False]");
    EXPECT_EQ(row4[2], "F1:type=<string>:str:\"s2\"");
    EXPECT_EQ(row4[3], "F2:type=<type>:type:<System.String, mscorlib>");
    EXPECT_EQ(row4[4], "F3:type=<string>:null");
}

TEST(CustomAttributeDecoderPairTest, DecodeValueDecodesNamedArgumentRows) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());

    // Row 5: four fixed arguments plus five named arguments.
    auto row5 = RenderRow(5, manifest, false);
    ASSERT_EQ(row5.size(), 10u);
    EXPECT_EQ(row5[0], "ok:F=4:N=5");
    EXPECT_EQ(row5[1], "F0:type=<bool[]>:arr:n=2:[bool:True|bool:True]");
    EXPECT_EQ(row5[2], "F1:type=<string>:str:\"n\"");
    EXPECT_EQ(row5[3], "F2:type=<type>:type:<System.Type>");
    EXPECT_EQ(row5[4], "F3:type=<bool>:bool:False");
    EXPECT_EQ(row5[5], "N0:name=pb:kind=Property:type=<bool>:bool:True");
    EXPECT_EQ(row5[6], "N1:name=fs:kind=Field:type=<string>:str:\"vs\"");
    EXPECT_EQ(row5[7],
        "N2:name=pi:kind=Property:type=<int32[]>:arr:n=3:[num:Int32:7|"
        "num:Int32:8|num:Int32:9]");
    EXPECT_EQ(row5[8], "N3:name=ft:kind=Field:type=<type>:type:<NS.MyEnum>");
    EXPECT_EQ(row5[9], "N4:name=ff:kind=Property:type=<float32>:float:1.5");
}

TEST(CustomAttributeDecoderPairTest, DecodeNamedArgumentsDecodesStandaloneBytes) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());

    PairProvider provider;
    TM::CustomAttributeDecoderT<PairProvider> decoder(manifest, provider);
    // The probe's standalone named-arg bytes (five arguments: a bool, a
    // string, an int32[], a Type, and an object carrying a string).
    const std::uint8_t namedBytes[] = {
        0x54, 0x02, 0x02, 0x70, 0x62, 0x01,
        0x53, 0x0E, 0x02, 0x66, 0x73, 0x02, 0x76, 0x73,
        0x54, 0x1D, 0x08, 0x02, 0x70, 0x69,
            0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
            0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
        0x53, 0x50, 0x02, 0x66, 0x74, 0x09,
            'N', 'S', '.', 'M', 'y', 'E', 'n', 'u', 'm',
        0x54, 0x51, 0x02, 0x70, 0x6F, 0x0E, 0x08,
            'b', 'o', 'x', 'e', 'd', '-', 'i', 'n',
    };
    std::size_t pos = 0;
    auto named = decoder.DecodeNamedArguments(namedBytes,
        sizeof(namedBytes), pos, 5);
    ASSERT_EQ(named.size(), 5u);
    ASSERT_EQ(pos, sizeof(namedBytes));

    EXPECT_EQ(named[0].Name(), "pb");
    EXPECT_EQ(named[0].Kind(), TS::CustomAttributeNamedArgumentKind::Property);
    EXPECT_EQ(named[0].Type(),
        (PairType{TM::PrimitiveTypeCode::Boolean, std::nullopt}));
    EXPECT_TRUE(std::any_cast<bool>(named[0].Value()));

    EXPECT_EQ(named[1].Name(), "fs");
    EXPECT_EQ(named[1].Kind(), TS::CustomAttributeNamedArgumentKind::Field);
    EXPECT_EQ(named[1].Type(),
        (PairType{TM::PrimitiveTypeCode::String, std::nullopt}));
    EXPECT_EQ(std::any_cast<std::string>(named[1].Value()), "vs");

    EXPECT_EQ(named[2].Name(), "pi");
    EXPECT_EQ(named[2].Type(),
        (PairType{TM::PrimitiveTypeCode::Int32, std::string("int32[]")}));
    const auto& pi = std::any_cast<std::vector<
        TS::CustomAttributeTypedArgumentT<PairType>>>(named[2].Value());
    ASSERT_EQ(pi.size(), 3u);
    EXPECT_EQ(std::any_cast<std::int32_t>(pi[0].Value()), 7);
    EXPECT_EQ(std::any_cast<std::int32_t>(pi[1].Value()), 8);
    EXPECT_EQ(std::any_cast<std::int32_t>(pi[2].Value()), 9);

    EXPECT_EQ(named[3].Name(), "ft");
    EXPECT_EQ(named[3].Type(), (PairType{kNoCode, std::string("type")}));
    EXPECT_EQ(std::any_cast<PairType>(named[3].Value()),
        (PairType{kNoCode, std::string("NS.MyEnum")}));

    EXPECT_EQ(named[4].Name(), "po");
    // The non-boxing TaggedObject named arg reports the DECODED type (the
    // C# DecodeArgument returns decoded.Type): the inner string pair, not
    // the declared object pair (only provideBoxingTypeInfo wraps the outer).
    EXPECT_EQ(named[4].Type(),
        (PairType{TM::PrimitiveTypeCode::String, std::nullopt}));
    EXPECT_EQ(std::any_cast<std::string>(named[4].Value()), "boxed-in");
}

TEST(CustomAttributeDecoderPairTest, BoxingDecoderBoxesTaggedObject) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());

    // The object-carrying-a-string named arg through the boxing decoder: the
    // outer pair (the declared object type) wraps the inner pair-typed
    // argument carrying the actual string.
    PairProvider provider;
    TM::CustomAttributeDecoderT<PairProvider> decoder(manifest, provider,
        true);
    const std::uint8_t namedBytes[] = {
        0x54, 0x51, 0x02, 0x70, 0x6F, 0x0E, 0x08,
            'b', 'o', 'x', 'e', 'd', '-', 'i', 'n',
    };
    std::size_t pos = 0;
    auto named = decoder.DecodeNamedArguments(namedBytes,
        sizeof(namedBytes), pos, 1);
    ASSERT_EQ(named.size(), 1u);
    EXPECT_EQ(named[0].Name(), "po");
    EXPECT_EQ(named[0].Type(),
        (PairType{TM::PrimitiveTypeCode::Object, std::nullopt}));
    const auto& boxed =
        std::any_cast<TS::CustomAttributeTypedArgumentT<PairType>>(
            named[0].Value());
    EXPECT_EQ(boxed.Type(),
        (PairType{TM::PrimitiveTypeCode::String, std::nullopt}));
    EXPECT_EQ(std::any_cast<std::string>(boxed.Value()), "boxed-in");
}

} // namespace
