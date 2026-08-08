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

#include "Decompiler/Metadata/SignatureDecoder.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ILSpy::Decompiler::Metadata {

using namespace ILSpy::Decompiler::TypeSystem;

namespace {
// Map an ECMA-335 ELEMENT_TYPE primitive to a KnownTypeCode. Returns None for
// non-primitive element types (Class/ValueType/GenericInst/Var/MVar/...).
KnownTypeCode PrimitiveCode(winmd::reader::ElementType e) {
    using ET = winmd::reader::ElementType;
    switch (e) {
        case ET::Void: return KnownTypeCode::Void;
        case ET::Boolean: return KnownTypeCode::Boolean;
        case ET::Char: return KnownTypeCode::Char;
        case ET::I1: return KnownTypeCode::SByte;
        case ET::U1: return KnownTypeCode::Byte;
        case ET::I2: return KnownTypeCode::Int16;
        case ET::U2: return KnownTypeCode::UInt16;
        case ET::I4: return KnownTypeCode::Int32;
        case ET::U4: return KnownTypeCode::UInt32;
        case ET::I8: return KnownTypeCode::Int64;
        case ET::U8: return KnownTypeCode::UInt64;
        case ET::R4: return KnownTypeCode::Single;
        case ET::R8: return KnownTypeCode::Double;
        case ET::String: return KnownTypeCode::String;
        case ET::Object: return KnownTypeCode::Object;
        case ET::I: return KnownTypeCode::IntPtr;
        case ET::U: return KnownTypeCode::UIntPtr;
        case ET::TypedByRef: return KnownTypeCode::TypedReference;
        default: return KnownTypeCode::None;
    }
}

// Look up a known type by namespace + name + arity. Linear scan of the known
// table; the table is small (~60) and this runs only for TypeDefOrRef resolution.
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

// Build a SimpleType (or KnownType) from a TypeDef/TypeRef name. The kind is
// unknown for a bare TypeRef, so default to Class; the type system refines it
// when it resolves the entity.
ITypePtr MakeTypeRef(std::string_view ns, std::string_view name, int arity) {
    KnownTypeCode kt = FindKnownType(ns, name, arity);
    if (kt != KnownTypeCode::None) return std::make_shared<KnownType>(kt);
    TopLevelTypeName tl(std::string(ns), std::string(name), arity);
    return std::make_shared<SimpleType>(std::move(tl));
}
} // namespace

ITypePtr ResolveTypeDefOrRef(const winmd::reader::database& db,
                             winmd::reader::coded_index<winmd::reader::TypeDefOrRef> cod) {
    using TDR = winmd::reader::TypeDefOrRef;
    if (cod.type() == TDR::TypeDef) {
        auto def = cod.TypeDef();
        auto ns = def.TypeNamespace();
        auto name = def.TypeName();
        return MakeTypeRef(ns, name, 0); // arity is on the generic params, not the name
    } else if (cod.type() == TDR::TypeRef) {
        auto ref = cod.TypeRef();
        return MakeTypeRef(ref.TypeNamespace(), ref.TypeName(), 0);
    } else if (cod.type() == TDR::TypeSpec) {
        // A TypeSpec carries a signature blob (always a generic instantiation,
        // array, or by-ref/ptr over a generic). It does not appear in method or
        // field signatures -- only in operand blobs (ldtoken, generic MethodSpec
        // call targets) which the Phase 3 IL reader decodes. Deferred: return
        // the UnknownType null object so callers degrade gracefully.
        return UnknownType();
    }
    return UnknownType();
}

ITypePtr DecodeType(const winmd::reader::database& db,
                    const winmd::reader::TypeSig& sig) {
    using ET = winmd::reader::ElementType;
    ITypePtr core;

    // Dispatch on the parsed value_type variant; element_type() alone is
    // ambiguous for primitives (the variant also holds the ElementType).
    const auto& v = sig.Type();
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, ET>) {
            KnownTypeCode kt = PrimitiveCode(arg);
            core = (kt != KnownTypeCode::None) ? std::make_shared<KnownType>(kt) : UnknownType();
        } else if constexpr (std::is_same_v<T, winmd::reader::coded_index<winmd::reader::TypeDefOrRef>>) {
            core = ResolveTypeDefOrRef(db, arg);
        } else if constexpr (std::is_same_v<T, winmd::reader::GenericTypeIndex>) {
            core = std::make_shared<TypeParameter>(static_cast<int>(arg.index),
                                                   TypeParameter::OwnerKind::Class, "");
        } else if constexpr (std::is_same_v<T, winmd::reader::GenericMethodTypeIndex>) {
            core = std::make_shared<TypeParameter>(static_cast<int>(arg.index),
                                                   TypeParameter::OwnerKind::Method, "");
        } else if constexpr (std::is_same_v<T, winmd::reader::GenericTypeInstSig>) {
            auto gen = ResolveTypeDefOrRef(db, arg.GenericType());
            std::vector<ITypePtr> args;
            auto [b, e] = arg.GenericArgs();
            for (auto it = b; it != e; ++it) args.push_back(DecodeType(db, *it));
            core = std::make_shared<ParameterizedType>(std::move(gen), std::move(args));
        } else {
            core = UnknownType();
        }
    }, v);

    // Apply wrappers: winmd parses SZArray/Array/Ptr prefixes before the core
    // type, so the core is the innermost element and wrappers go outward.
    // (Ptr is applied innermost-after-array; nested SZArray/Ptr combos are rare
    // in framework IL and refined later.)
    if (sig.is_szarray()) {
        core = std::make_shared<ArrayType>(std::move(core));
    } else if (sig.is_array()) {
        core = std::make_shared<ArrayType>(std::move(core), static_cast<int>(sig.array_rank()));
    }
    for (int p = 0; p < sig.ptr_count(); ++p) {
        core = std::make_shared<PointerType>(std::move(core));
    }
    return core;
}

DecodedMethodSignature DecodeMethodSignature(const winmd::reader::database& db,
                                              const winmd::reader::MethodDefSig& sig) {
    DecodedMethodSignature out;
    out.GenericParameterCount = sig.GenericParamCount();
    // CallingConvention has HASThis bit; winmd exposes it via the enum.
    using CC = winmd::reader::CallingConvention;
    out.IsInstance = (enum_mask(sig.CallConvention(), CC::HasThis) == CC::HasThis);

    // Return type. RetTypeSig leaves its TypeSig disengaged for `void` returns
    // (its `explicit operator bool` reports has_value), so check before calling
    // Type(); a void return decodes to the KnownType Void.
    const auto& ret = sig.ReturnType();
    if (ret) {
        out.ReturnType = DecodeType(db, ret.Type());
        if (ret.ByRef()) {
            out.ReturnType = std::make_shared<ByReferenceType>(std::move(out.ReturnType));
        }
    } else {
        out.ReturnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    }

    auto [pb, pe] = sig.Params();
    for (auto it = pb; it != pe; ++it) {
        ITypePtr t = DecodeType(db, it->Type());
        if (it->ByRef()) t = std::make_shared<ByReferenceType>(std::move(t));
        out.ParameterTypes.push_back(std::move(t));
    }
    return out;
}

TypeSystem::ITypePtr DecodeFieldSignature(const winmd::reader::database& db,
                                          const winmd::reader::FieldSig& sig) {
    return DecodeType(db, sig.Type());
}

} // namespace ILSpy::Decompiler::Metadata
