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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// ECMA-335 signature decoding (II.23.2) onto the IType model. The blob parser
// is hand-rolled: the vendored winmd TypeSig window only covers the Windows
// Runtime subset and throws on TypedByRef, sentinel (varargs), fn-ptr, and
// custom modifiers -- all of which the framework's method signatures carry.
// Everything the WinMD profile lacks decodes here; malformed blobs fail the
// decode (nullopt at the MetadataFile callers) per the robustness tenet.

#include "Decompiler/Metadata/SignatureDecoder.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

using namespace ILSpy::Decompiler::TypeSystem;

namespace {

// Map an ECMA-335 ELEMENT_TYPE primitive byte to a KnownTypeCode. Returns
// None for non-primitive element types.
KnownTypeCode PrimitiveCode(std::uint32_t et) {
    switch (et) {
        case 0x01: return KnownTypeCode::Void;        // Void
        case 0x02: return KnownTypeCode::Boolean;     // Boolean
        case 0x03: return KnownTypeCode::Char;        // Char
        case 0x04: return KnownTypeCode::SByte;       // I1
        case 0x05: return KnownTypeCode::Byte;        // U1
        case 0x06: return KnownTypeCode::Int16;       // I2
        case 0x07: return KnownTypeCode::UInt16;      // U2
        case 0x08: return KnownTypeCode::Int32;       // I4
        case 0x09: return KnownTypeCode::UInt32;      // U4
        case 0x0A: return KnownTypeCode::Int64;       // I8
        case 0x0B: return KnownTypeCode::UInt64;      // U8
        case 0x0C: return KnownTypeCode::Single;      // R4
        case 0x0D: return KnownTypeCode::Double;      // R8
        case 0x0E: return KnownTypeCode::String;      // String
        case 0x16: return KnownTypeCode::TypedReference;  // TypedByRef
        case 0x18: return KnownTypeCode::IntPtr;      // I
        case 0x19: return KnownTypeCode::UIntPtr;     // U
        case 0x1C: return KnownTypeCode::Object;      // Object
        default: return KnownTypeCode::None;
    }
}

// Look up a known type by namespace + name + arity. Linear scan of the known
// table; the table is small (~60) and this runs only for type resolution.
KnownTypeCode FindKnownType(std::string_view ns, std::string_view name, int arity) {
    const auto* tab = KnownTypeTable();
    for (std::size_t i = 1; i < KnownTypeTableSize(); ++i) {
        const auto& r = tab[i];
        if (r.Namespace == ns && r.Name == name && r.TypeParameterCount == arity) {
            return r.Code;
        }
    }
    return KnownTypeCode::None;
}

} // namespace

// Look up a known type by namespace + name + arity; fall back to SimpleType.
ITypePtr MakeTypeRef(std::string_view ns, std::string_view name, int arity) {
    KnownTypeCode kt = FindKnownType(ns, name, arity);
    if (kt != KnownTypeCode::None) return std::make_shared<KnownType>(kt);
    TopLevelTypeName tl(std::string(ns), std::string(name), arity);
    return std::make_shared<SimpleType>(std::move(tl));
}

namespace {

// Cursor over a signature blob. Every read checks bounds and latches
// `failed`; callers must check Failed() (or a null return) before using
// results. All multi-byte fields follow ECMA-335 compressed-integer form.
struct BlobReader {
    const std::uint8_t* cur;
    const std::uint8_t* end;
    const winmd::reader::database* db;
    bool failed = false;

    std::uint8_t Byte() {
        if (cur >= end) { failed = true; return 0; }
        return *cur++;
    }
    std::uint8_t PeekByte() {
        if (cur >= end) { failed = true; return 0; }
        return *cur;
    }
    std::uint32_t CompressedUnsigned() {
        std::uint8_t b0 = Byte();
        if (failed) return 0;
        if ((b0 & 0x80) == 0) return b0;
        if ((b0 & 0xC0) == 0x80)
            return (static_cast<std::uint32_t>(b0 & 0x3F) << 8) | Byte();
        return (static_cast<std::uint32_t>(b0 & 0x1F) << 24) |
               (static_cast<std::uint32_t>(Byte()) << 16) |
               (static_cast<std::uint32_t>(Byte()) << 8) | Byte();
    }
};

// Resolve a TypeDefOrRefEncoded compressed coded index (II.23.2.8): tag in the
// low 2 bits (0=TypeDef, 1=TypeRef, 2=TypeSpec) then the 1-based row.
ITypePtr DecodeTypeDefOrRefEncoded(BlobReader& r, std::uint32_t coded);

// Forward declaration for recursion (arrays carry element types, generic
// instantiations carry argument lists, TypeSpec wraps full blobs).
ITypePtr DecodeTypeBlob(BlobReader& r);

ITypePtr DecodeTypeSpecRow(BlobReader& r, std::uint32_t row /*1-based*/) {
    if (row == 0 || row > r.db->TypeSpec.size()) { r.failed = true; return UnknownType(); }
    // TypeSpec column 0 is the signature blob heap index.
    std::uint32_t blobIndex = r.db->TypeSpec.get_value<std::uint32_t>(row - 1, 0);
    auto view = r.db->get_blob(blobIndex);
    BlobReader inner{ view.begin(), view.end(), r.db, false };
    ITypePtr t = DecodeTypeBlob(inner);
    if (inner.failed || inner.cur != inner.end) r.failed = true;  // must consume exactly
    return t;
}

ITypePtr DecodeTypeDefOrRefEncoded(BlobReader& r, std::uint32_t coded) {
    std::uint32_t tag = coded & 3;
    std::uint32_t row = coded >> 2;  // 1-based
    if (tag == 0 && row && row <= r.db->TypeDef.size()) {          // TypeDef
        auto def = r.db->TypeDef[row - 1];
        return MakeTypeRef(def.TypeNamespace(), def.TypeName(), 0);
    }
    if (tag == 1 && row && row <= r.db->TypeRef.size()) {          // TypeRef
        auto ref = r.db->TypeRef[row - 1];
        return MakeTypeRef(ref.TypeNamespace(), ref.TypeName(), 0);
    }
    if (tag == 2) {                                                // TypeSpec
        return DecodeTypeSpecRow(r, row);
    }
    r.failed = true;
    return UnknownType();
}

// Parse a method signature nested inside another type (FnPtr elements). The
// parsed shape is discarded; this only advances the cursor correctly.
void SkipMethodSignature(BlobReader& r) {
    std::uint32_t callconv = r.Byte();
    if (r.failed) return;
    if (callconv & 0x10) r.CompressedUnsigned();  // GENERIC: gen param count
    std::uint32_t paramCount = r.CompressedUnsigned();
    (void)DecodeTypeBlob(r);  // return type (its prefixes incl. ByRef/custom mods)
    for (std::uint32_t i = 0; i < paramCount && !r.failed; ++i) {
        if (r.PeekByte() == 0x41) { r.Byte(); }  // Sentinel before optional params
        (void)DecodeTypeBlob(r);
    }
}

ITypePtr DecodeTypeBlob(BlobReader& r) {
    // Custom modifiers and the PINNED marker prefix the type; skip them.
    for (;;) {
        std::uint8_t b = r.PeekByte();
        if (r.failed) return UnknownType();
        if (b == 0x1F || b == 0x20) {  // CModReqd / CModOpt
            r.Byte();
            r.CompressedUnsigned();  // the modifier's TypeDefOrRef
            continue;
        }
        if (b == 0x45) {  // Pinned
            r.Byte();
            continue;
        }
        break;
    }

    std::uint32_t et = r.Byte();
    if (r.failed) return UnknownType();

    KnownTypeCode prim = PrimitiveCode(et);
    if (prim != KnownTypeCode::None) return std::make_shared<KnownType>(prim);

    switch (et) {
        case 0x0F:  // Ptr
            return std::make_shared<PointerType>(DecodeTypeBlob(r));
        case 0x10:  // ByRef
            return std::make_shared<ByReferenceType>(DecodeTypeBlob(r));
        case 0x11:  // ValueType
        case 0x12:  // Class
            return DecodeTypeDefOrRefEncoded(r, r.CompressedUnsigned());
        case 0x13:  // Var (class generic parameter)
            return std::make_shared<TypeParameter>(static_cast<int>(r.CompressedUnsigned()),
                                                   TypeParameter::OwnerKind::Class, "");
        case 0x1E:  // MVar (method generic parameter)
            return std::make_shared<TypeParameter>(static_cast<int>(r.CompressedUnsigned()),
                                                   TypeParameter::OwnerKind::Method, "");
        case 0x1D:  // SZArray
            return std::make_shared<ArrayType>(DecodeTypeBlob(r));
        case 0x14: {  // Array (multi-dimensional, possibly non-zero-bounded)
            ITypePtr elem = DecodeTypeBlob(r);
            std::uint32_t rank = r.CompressedUnsigned();
            std::uint32_t numSizes = r.CompressedUnsigned();
            for (std::uint32_t i = 0; i < numSizes && !r.failed; ++i) r.CompressedUnsigned();
            std::uint32_t numLoBounds = r.CompressedUnsigned();
            for (std::uint32_t i = 0; i < numLoBounds && !r.failed; ++i) r.CompressedUnsigned();
            return std::make_shared<ArrayType>(std::move(elem), static_cast<int>(rank));
        }
        case 0x15: {  // GenericInst
            r.Byte();  // CLASS (0x12) or VALUETYPE (0x11) marker
            ITypePtr def = DecodeTypeDefOrRefEncoded(r, r.CompressedUnsigned());
            std::uint32_t argCount = r.CompressedUnsigned();
            std::vector<ITypePtr> args;
            args.reserve(argCount);
            for (std::uint32_t i = 0; i < argCount && !r.failed; ++i)
                args.push_back(DecodeTypeBlob(r));
            return std::make_shared<ParameterizedType>(std::move(def), std::move(args));
        }
        case 0x1B:  // FnPtr: a full embedded method signature. Not modeled;
            SkipMethodSignature(r);   // IType has no function-pointer node yet.
            return UnknownType();
        default:
            r.failed = true;
            return UnknownType();
    }
}

} // namespace

ITypePtr DecodeTypeSpecBlob(const winmd::reader::database& db,
                            const std::uint8_t* data, std::size_t size) {
    BlobReader r{ data, data + size, &db, false };
    ITypePtr t = DecodeTypeBlob(r);
    if (r.failed) return nullptr;
    return t;
}

ITypePtr DecodeFieldSignatureBlob(const winmd::reader::database& db,
                                  const std::uint8_t* data, std::size_t size) {
    BlobReader r{ data, data + size, &db, false };
    std::uint32_t marker = r.Byte();  // 0x06 = FIELD
    if (r.failed || marker != 0x06) return nullptr;
    ITypePtr t = DecodeTypeBlob(r);
    if (r.failed) return nullptr;
    return t;
}

DecodedMethodSignature DecodeMethodSignatureBlob(const winmd::reader::database& db,
                                                 const std::uint8_t* data, std::size_t size,
                                                 bool& ok) {
    ok = false;
    DecodedMethodSignature out;
    BlobReader r{ data, data + size, &db, false };

    std::uint32_t callconv = r.Byte();
    if (r.failed) return out;
    out.IsInstance = (callconv & 0x20) != 0;  // HASTHIS
    if (callconv & 0x10)  // GENERIC
        out.GenericParameterCount = r.CompressedUnsigned();
    std::uint32_t paramCount = r.CompressedUnsigned();

    // Return type: void is the bare 0x01; otherwise a full type with the
    // byref/custom-modifier prefixes allowed (TypedByRef included).
    if (r.PeekByte() == 0x01) {
        r.Byte();
        out.ReturnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    } else {
        out.ReturnType = DecodeTypeBlob(r);
    }
    if (r.failed) return DecodedMethodSignature{};

    for (std::uint32_t i = 0; i < paramCount && !r.failed; ++i) {
        // Vararg signatures place a SENTINEL element before the optional
        // parameters; it is not itself a parameter.
        if (r.PeekByte() == 0x41) r.Byte();
        out.ParameterTypes.push_back(DecodeTypeBlob(r));
    }
    if (r.failed) return DecodedMethodSignature{};
    ok = true;
    return out;
}

std::vector<ITypePtr> DecodeLocalSignatureBlob(const winmd::reader::database& db,
                                               const std::uint8_t* data, std::size_t size) {
    std::vector<ITypePtr> result;
    BlobReader r{ data, data + size, &db, false };
    std::uint32_t marker = r.Byte();
    if (r.failed || marker != 0x07) return result;  // IMAGE_CEE_CS_CALLCONV_LOCAL_SIG
    std::uint32_t count = r.CompressedUnsigned();
    if (r.failed) return result;
    for (std::uint32_t i = 0; i < count && !r.failed; ++i) {
        // A local may be pinned (0x45 PINNED) before its type, and may be a
        // by-ref; DecodeTypeBlob handles TYPEDBYREF and custom modifiers.
        if (r.PeekByte() == 0x45) r.Byte();  // PINNED modifier
        result.push_back(DecodeTypeBlob(r));
    }
    if (r.failed) result.clear();
    return result;
}

} // namespace ILSpy::Decompiler::Metadata
