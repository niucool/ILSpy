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

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/Metadata/MethodBodyReader.hpp"
#include "Decompiler/Metadata/SignatureDecoder.hpp"
#include "Decompiler/TypeSystem/TypeKindDerivation.hpp"

#include "Decompiler/Metadata/Ecma335/WinmdInclude.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

namespace {
// Read the whole file into a buffer. Used to drive method-body decoding
// (RVA -> file offset) independently of winmd's own mmap of the metadata
// streams. Returns nullptr if the file cannot be read.
std::shared_ptr<const std::vector<std::uint8_t>> ReadAllBytes(std::string_view path) {
    std::ifstream f((std::string{path}), std::ios::binary);
    if (!f) return nullptr;
    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz < 0) return nullptr;
    bytes->resize(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes->data()), bytes->size());
    if (f.gcount() != static_cast<std::streamsize>(bytes->size())) return nullptr;
    return bytes;
}
} // namespace

struct MetadataFile::Impl {
    std::string path;
    bool valid = false;
    std::unique_ptr<winmd::reader::database> db;
    std::shared_ptr<const std::vector<std::uint8_t>> image;  // for method bodies
    std::unique_ptr<MethodBodyReader> bodyReader;

    explicit Impl(std::string_view p) : path(p) {
        // winmd throws std::invalid_argument for a missing/unreadable file (out
        // of is_database()'s file_view ctor) and for a malformed image (out of
        // database construction). The decompiler degrades gracefully rather than
        // propagating those, so any failure leaves the file reported as invalid.
        try {
            if (!winmd::reader::database::is_database(p)) return;
            db = std::make_unique<winmd::reader::database>(p);
            image = ReadAllBytes(p);
            if (image) bodyReader = std::make_unique<MethodBodyReader>(image);
            valid = true;
        } catch (const std::exception&) {
            db.reset();
            valid = false;
        }
    }
};

MetadataFile::MetadataFile(std::string_view path)
    : impl_(std::make_unique<Impl>(path)) {}

MetadataFile::~MetadataFile() = default;

MetadataFile::MetadataFile(MetadataFile&&) noexcept = default;
MetadataFile& MetadataFile::operator=(MetadataFile&&) noexcept = default;

bool MetadataFile::IsValid() const noexcept {
    return impl_ && impl_->valid;
}

std::uint32_t MetadataFile::TypeDefCount() const noexcept {
    return IsValid() ? impl_->db->TypeDef.size() : 0;
}

std::vector<std::string> MetadataFile::TopTypeNames(std::size_t n) const {
    std::vector<std::string> result;
    if (!IsValid()) return result;
    result.reserve(n);
    for (auto&& type : impl_->db->TypeDef) {
        if (result.size() >= n) break;
        result.emplace_back(std::string{ type.TypeName() });
    }
    return result;
}

std::vector<MethodDefInfo> MetadataFile::MethodDefs() const {
    std::vector<MethodDefInfo> result;
    if (!IsValid()) return result;
    // MethodDef tokens are table 0x06; rows are 1-based. The IL reader, type
    // system, and metadata token helpers all key off these tokens, so materialise
    // them here even though only RVA/Name are needed for method-body decoding.
    std::uint32_t index = 1;
    for (auto&& m : impl_->db->MethodDef) {
        MethodDefInfo info;
        info.Name = std::string{ m.Name() };
        info.RVA = m.RVA();
        info.Token = (0x06u << 24) | (index & 0x00FFFFFFu);
        result.push_back(std::move(info));
        ++index;
    }
    return result;
}

std::vector<TypeDefInfo> MetadataFile::TypeDefs() const {
    std::vector<TypeDefInfo> result;
    if (!IsValid()) return result;
    std::uint32_t index = 1;
    for (auto&& t : impl_->db->TypeDef) {
        TypeDefInfo info;
        info.Name = std::string{ t.TypeName() };
        info.Namespace = std::string{ t.TypeNamespace() };
        info.Token = (0x02u << 24) | (index & 0x00FFFFFFu);
        info.Flags = t.Flags().value;
        auto extends = t.Extends();
        if (extends) {
            try { info.BaseType = ResolveTypeDefOrRef(*impl_->db, extends); }
            catch (const std::exception&) { info.BaseType = nullptr; }
        }
        // Derive Kind from the flags + base type (matches MetadataTypeDefinition.cs
        // + SRMExtensions). selfRefName is the dotted "Namespace.Name" form.
        std::string selfRefName = info.Namespace.empty()
            ? info.Name : info.Namespace + "." + info.Name;
        info.Kind = ILSpy::Decompiler::TypeSystem::DeriveTypeKind(
            info.Flags, info.BaseType, selfRefName);
        result.push_back(std::move(info));
        ++index;
    }
    return result;
}

std::vector<MethodInfo> MetadataFile::GetMethods(std::uint32_t typeToken) const {
    std::vector<MethodInfo> result;
    if (!IsValid()) return result;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->TypeDef.size()) return result;
    auto t = impl_->db->TypeDef[row - 1];
    auto range = t.MethodList();
    for (auto it = range.first; it != range.second; ++it) {
        MethodInfo m;
        m.Name = std::string{ (*it).Name() };
        m.RVA = (*it).RVA();
        // MethodDef token: table 0x06, row is 1-based index into MethodDef.
        std::uint32_t methodRow = static_cast<std::uint32_t>((*it).index()) + 1;
        m.Token = (0x06u << 24) | (methodRow & 0x00FFFFFFu);
        result.push_back(std::move(m));
    }
    return result;
}

std::vector<FieldInfo> MetadataFile::GetFields(std::uint32_t typeToken) const {
    std::vector<FieldInfo> result;
    if (!IsValid()) return result;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->TypeDef.size()) return result;
    auto t = impl_->db->TypeDef[row - 1];
    auto range = t.FieldList();
    for (auto it = range.first; it != range.second; ++it) {
        FieldInfo f;
        f.Name = std::string{ (*it).Name() };
        std::uint32_t fieldRow = static_cast<std::uint32_t>((*it).index()) + 1;
        f.Token = (0x04u << 24) | (fieldRow & 0x00FFFFFFu);
        result.push_back(std::move(f));
    }
    return result;
}

std::vector<PropertyInfo> MetadataFile::GetProperties(std::uint32_t typeToken) const {
    std::vector<PropertyInfo> result;
    if (!IsValid()) return result;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->TypeDef.size()) return result;
    auto t = impl_->db->TypeDef[row - 1];
    auto range = t.PropertyList();
    for (auto it = range.first; it != range.second; ++it) {
        PropertyInfo p;
        p.Name = std::string{ (*it).Name() };
        std::uint32_t propRow = static_cast<std::uint32_t>((*it).index()) + 1;
        p.Token = (0x17u << 24) | (propRow & 0x00FFFFFFu);
        result.push_back(std::move(p));
    }
    return result;
}

ILSpy::Decompiler::TypeSystem::ITypePtr MetadataFile::GetFieldSignature(std::uint32_t fieldToken) const {
    if (!IsValid()) return nullptr;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->Field.size()) return nullptr;
    try {
        auto f = impl_->db->Field[row - 1];
        return DecodeFieldSignature(*impl_->db, f.Signature());
    } catch (const std::exception&) {
        return nullptr;
    }
}

std::vector<CustomAttributeInfo> MetadataFile::GetCustomAttributes(std::uint32_t entityToken) const {
    std::vector<CustomAttributeInfo> result;
    if (!IsValid()) return result;
    std::uint32_t table = entityToken >> 24;
    std::uint32_t row = entityToken & 0x00FFFFFFu;
    // Collect the attribute type namespace+name from each CustomAttribute row in
    // the entity's range. TypeNamespaceAndName() reads names directly (no cache);
    // it throws for an unexpected MemberRefParent, so per-attribute try/catch.
    auto collect = [&](auto range) {
        for (auto it = range.first; it != range.second; ++it) {
            try {
                auto ns_name = (*it).TypeNamespaceAndName();
                result.push_back({ std::string(ns_name.first), std::string(ns_name.second) });
            } catch (const std::exception&) {
                // Skip an attribute whose type name cannot be resolved.
            }
        }
    };
    try {
        if (table == 0x02) {  // TypeDef
            if (row == 0 || row > impl_->db->TypeDef.size()) return result;
            collect(impl_->db->TypeDef[row - 1].CustomAttribute());
        } else if (table == 0x04) {  // Field
            if (row == 0 || row > impl_->db->Field.size()) return result;
            collect(impl_->db->Field[row - 1].CustomAttribute());
        } else if (table == 0x06) {  // MethodDef
            if (row == 0 || row > impl_->db->MethodDef.size()) return result;
            collect(impl_->db->MethodDef[row - 1].CustomAttribute());
        } else if (table == 0x17) {  // Property
            if (row == 0 || row > impl_->db->Property.size()) return result;
            collect(impl_->db->Property[row - 1].CustomAttribute());
        }
    } catch (const std::exception&) {
        // Malformed image: return whatever was collected so far.
    }
    return result;
}

MethodBody MetadataFile::GetMethodBody(std::uint32_t rva) const {
    if (!IsValid() || !impl_->bodyReader) return MethodBody{};
    return impl_->bodyReader->Read(rva);
}

std::optional<MethodSignature> MetadataFile::GetMethodSignature(std::uint32_t methodToken) const {
    if (!IsValid()) return std::nullopt;
    // MethodDef tokens are table 0x06; rows are 1-based, the table is 0-indexed.
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->MethodDef.size()) return std::nullopt;
    try {
        auto m = impl_->db->MethodDef[row - 1];
        auto sig = m.Signature();
        DecodedMethodSignature d = DecodeMethodSignature(*impl_->db, sig);
        MethodSignature out;
        out.ReturnType = std::move(d.ReturnType);
        out.ParameterTypes = std::move(d.ParameterTypes);
        out.IsInstance = d.IsInstance;
        out.GenericParameterCount = d.GenericParameterCount;
        return out;
    } catch (const std::exception&) {
        // Malformed signature blob: degrade to std::nullopt rather than throwing,
        // matching the decompiler's robustness tenet.
        return std::nullopt;
    }
}

} // namespace ILSpy::Decompiler::Metadata
