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
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

namespace {

// Authors' generic parameter names for a method's decode scope (D172/D173):
// MethodDef.GenericParam rows name this method's MVAR (!!N) params; the
// declaring TypeDef's GenericParam rows name the class's VAR (!N) params.
// Lookups are 0-based via the GenericParam.Number column; rows need not be
// table-ordered in the GenericParam table. Returns an empty context when the
// row index is out of range (callers then get positional fallback names).
GenericParamNames BuildGenericParamNames(const winmd::reader::database& db,
                                         std::uint32_t methodRow /*1-based*/) {
    GenericParamNames names;
    if (methodRow == 0 || methodRow > db.MethodDef.size()) return names;
    auto md = db.MethodDef[methodRow - 1];
    auto collectInto = [](auto range, std::vector<std::string>& out) {
        for (auto it = range.first; it != range.second; ++it) {
            std::uint32_t n = (*it).Number();
            if (n >= out.size()) out.resize(n + 1);
            out[n] = std::string((*it).Name());
        }
    };
    collectInto(md.GenericParam(), names.methodNames);
    collectInto(md.Parent().GenericParam(), names.classNames);  // declaring TypeDef
    return names;
}


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

std::uint32_t MetadataFile::TypeRefCount() const noexcept {
    return IsValid() ? impl_->db->TypeRef.size() : 0;
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

namespace {
// Resolve a TypeDefOrRef coded index (from row accessors like TypeDef::Extends)
// to the IType name model.
ILSpy::Decompiler::TypeSystem::ITypePtr ResolveTypeDefOrRefIndex(
        const winmd::reader::coded_index<winmd::reader::TypeDefOrRef>& cod) {
    using TDR = winmd::reader::TypeDefOrRef;
    if (!cod) return nullptr;
    if (cod.type() == TDR::TypeDef) {
        auto d = cod.TypeDef();
        return MakeTypeRef(d.TypeNamespace(), d.TypeName(), 0);
    }
    if (cod.type() == TDR::TypeRef) {
        auto r = cod.TypeRef();
        return MakeTypeRef(r.TypeNamespace(), r.TypeName(), 0);
    }
    // TypeSpec bases do not occur in compiler-emitted TypeDef.Extends.
    return nullptr;
}
} // namespace

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
            try { info.BaseType = ResolveTypeDefOrRefIndex(extends); }
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

std::vector<EventInfo> MetadataFile::GetEvents(std::uint32_t typeToken) const {
    std::vector<EventInfo> result;
    if (!IsValid()) return result;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->TypeDef.size()) return result;
    auto t = impl_->db->TypeDef[row - 1];
    auto range = t.EventList();
    for (auto it = range.first; it != range.second; ++it) {
        EventInfo e;
        e.Name = std::string{ (*it).Name() };
        // Event tokens are table 0x14; rows are 1-based (the GetMethods
        // convention).
        std::uint32_t eventRow = static_cast<std::uint32_t>((*it).index()) + 1;
        e.Token = (0x14u << 24) | (eventRow & 0x00FFFFFFu);
        result.push_back(std::move(e));
    }
    return result;
}

std::uint32_t MetadataFile::GetTypeDefAttributes(std::uint32_t typeToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = typeToken >> 24;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (table != 0x02 || row == 0 || row > impl_->db->TypeDef.size()) return 0;
    try {
        // TypeDef flags are a 4-byte column (II.23.1.15).
        return impl_->db->TypeDef[row - 1].Flags().value;
    } catch (const std::exception&) {
        return 0;
    }
}

std::uint32_t MetadataFile::GetFieldAttributes(std::uint32_t fieldToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = fieldToken >> 24;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (table != 0x04 || row == 0 || row > impl_->db->Field.size()) return 0;
    try {
        // Field flags are a 2-byte column (II.23.1.5), widened to uint32 (the
        // BCL enum reading a row widens to int).
        return impl_->db->Field[row - 1].Flags().value;
    } catch (const std::exception&) {
        return 0;
    }
}

std::uint32_t MetadataFile::GetMethodAttributes(std::uint32_t methodToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return 0;
    try {
        // MethodDef flags are a 2-byte column (II.23.1.10), widened to uint32.
        return impl_->db->MethodDef[row - 1].Flags().value;
    } catch (const std::exception&) {
        return 0;
    }
}

std::uint32_t MetadataFile::GetPropertyAttributes(std::uint32_t propertyToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = propertyToken >> 24;
    std::uint32_t row = propertyToken & 0x00FFFFFFu;
    if (table != 0x17 || row == 0 || row > impl_->db->Property.size()) return 0;
    try {
        // Property flags are a 2-byte II.23.1 column, widened to uint32.
        return impl_->db->Property[row - 1].Flags().value;
    } catch (const std::exception&) {
        return 0;
    }
}

std::uint32_t MetadataFile::GetEventAttributes(std::uint32_t eventToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = eventToken >> 24;
    std::uint32_t row = eventToken & 0x00FFFFFFu;
    if (table != 0x14 || row == 0 || row > impl_->db->Event.size()) return 0;
    try {
        // Event flags are a 2-byte II.23.1 column, widened to uint32.
        return impl_->db->Event[row - 1].EventFlags().value;
    } catch (const std::exception&) {
        return 0;
    }
}

std::vector<GenericParameterInfo> MetadataFile::GetGenericParameters(std::uint32_t ownerToken) const {
    std::vector<GenericParameterInfo> result;
    if (!IsValid()) return result;
    std::uint32_t table = ownerToken >> 24;
    std::uint32_t row = ownerToken & 0x00FFFFFFu;
    if (row == 0) return result;
    try {
        if (table == 0x02) {
            if (row > impl_->db->TypeDef.size()) return result;
            auto range = impl_->db->TypeDef[row - 1].GenericParam();
            for (auto it = range.first; it != range.second; ++it) {
                GenericParameterInfo gp;
                // GenericParam tokens are table 0x2A; winmd's row_base::index()
                // is 0-based, so the 1-based token RID is index()+1.
                gp.Token = (0x2Au << 24) | ((static_cast<std::uint32_t>((*it).index()) + 1) & 0x00FFFFFFu);
                gp.Number = (*it).Number();
                gp.Flags = (*it).Flags().value;
                gp.Name = std::string{ (*it).Name() };
                result.push_back(std::move(gp));
            }
        } else if (table == 0x06) {
            if (row > impl_->db->MethodDef.size()) return result;
            auto range = impl_->db->MethodDef[row - 1].GenericParam();
            for (auto it = range.first; it != range.second; ++it) {
                GenericParameterInfo gp;
                gp.Token = (0x2Au << 24) | ((static_cast<std::uint32_t>((*it).index()) + 1) & 0x00FFFFFFu);
                gp.Number = (*it).Number();
                gp.Flags = (*it).Flags().value;
                gp.Name = std::string{ (*it).Name() };
                result.push_back(std::move(gp));
            }
        }
    } catch (const std::exception&) {
        // A malformed table walk degrades to the empty result (never throws).
        result.clear();
    }
    return result;
}

std::uint32_t MetadataFile::GetMethodDeclaringTypeToken(std::uint32_t methodToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return 0;
    try {
        auto t = impl_->db->MethodDef[row - 1].Parent();
        if (!t) return 0;
        return (0x02u << 24) | ((static_cast<std::uint32_t>(t.index()) + 1) & 0x00FFFFFFu);
    } catch (const std::exception&) {
        return 0;
    }
}

// A TypeDef row's Name/Namespace columns and its declaring TypeDef's token
// (the NestedClass-table walk -- the SRM TypeDefinition.GetDeclaringType()
// analog). See the header for the full contract.
std::optional<TypeDefNameInfo> MetadataFile::GetTypeDefNameInfo(std::uint32_t typeToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = typeToken >> 24;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (table != 0x02 || row == 0 || row > impl_->db->TypeDef.size()) return std::nullopt;
    try {
        auto t = impl_->db->TypeDef[row - 1];
        TypeDefNameInfo info;
        info.Name = std::string{ t.TypeName() };
        info.Namespace = std::string{ t.TypeNamespace() };
        auto enclosing = t.EnclosingType();
        if (enclosing) {
            info.DeclaringTypeToken =
                (0x02u << 24) | ((static_cast<std::uint32_t>(enclosing.index()) + 1) & 0x00FFFFFFu);
        }
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A TypeRef row's TypeName/TypeNamespace columns and its declaring TypeRef's
// token (the resolution-scope walk -- the SRMExtensions
// GetDeclaringType(this in TypeReference) analog: only a TypeRef-scoped row
// nests; Module/ModuleRef/AssemblyRef scopes are top-level). See the header
// for the full contract.
std::optional<TypeRefNameInfo> MetadataFile::GetTypeRefNameInfo(std::uint32_t typeRefToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = typeRefToken >> 24;
    std::uint32_t row = typeRefToken & 0x00FFFFFFu;
    if (table != 0x01 || row == 0 || row > impl_->db->TypeRef.size()) return std::nullopt;
    try {
        auto r = impl_->db->TypeRef[row - 1];
        TypeRefNameInfo info;
        info.Name = std::string{ r.TypeName() };
        info.Namespace = std::string{ r.TypeNamespace() };
        auto scope = r.ResolutionScope();
        using RS = winmd::reader::ResolutionScope;
        if (scope && scope.type() == RS::TypeRef) {
            auto declaring = scope.TypeRef();
            info.DeclaringTypeRefToken =
                (0x01u << 24) | ((static_cast<std::uint32_t>(declaring.index()) + 1) & 0x00FFFFFFu);
        }
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A TypeRef row's ResolutionScope coded index: the scope kind plus the
// scope row's name string / raw token. See the header for the full contract.
std::optional<TypeRefScopeInfo> MetadataFile::GetTypeRefScopeInfo(std::uint32_t typeRefToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = typeRefToken >> 24;
    std::uint32_t row = typeRefToken & 0x00FFFFFFu;
    if (table != 0x01 || row == 0 || row > impl_->db->TypeRef.size()) return std::nullopt;
    try {
        auto r = impl_->db->TypeRef[row - 1];
        auto scope = r.ResolutionScope();
        using RS = winmd::reader::ResolutionScope;
        TypeRefScopeInfo info;
        if (!scope) {
            info.Scope = TypeRefScopeInfo::Kind::None;
            return info;
        }
        switch (scope.type()) {
            case RS::Module: {
                auto m = scope.Module();
                info.Scope = TypeRefScopeInfo::Kind::Module;
                info.ScopeToken = 0x00u << 24;
                info.Name = std::string{ m.Name() };
                break;
            }
            case RS::ModuleRef: {
                auto mr = scope.ModuleRef();
                info.Scope = TypeRefScopeInfo::Kind::ModuleRef;
                info.ScopeToken = (0x1Au << 24) |
                    ((static_cast<std::uint32_t>(mr.index()) + 1) & 0x00FFFFFFu);
                // The winmd ModuleRef row carries only the name column with no
                // public accessor; the C# WriteTo renders nothing for a
                // ModuleReference scope, so the empty name is faithful.
                break;
            }
            case RS::AssemblyRef: {
                auto ar = scope.AssemblyRef();
                info.Scope = TypeRefScopeInfo::Kind::AssemblyRef;
                info.ScopeToken = (0x23u << 24) |
                    ((static_cast<std::uint32_t>(ar.index()) + 1) & 0x00FFFFFFu);
                info.Name = std::string{ ar.Name() };
                break;
            }
            case RS::TypeRef: {
                auto tr = scope.TypeRef();
                info.Scope = TypeRefScopeInfo::Kind::TypeRef;
                info.ScopeToken = (0x01u << 24) |
                    ((static_cast<std::uint32_t>(tr.index()) + 1) & 0x00FFFFFFu);
                break;
            }
        }
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A GenericParam row (table 0x2A) by raw token: Token/Number/Name. See the
// header for the full contract.
std::optional<GenericParameterInfo> MetadataFile::GetGenericParameterByToken(
        std::uint32_t genericParamToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = genericParamToken >> 24;
    std::uint32_t row = genericParamToken & 0x00FFFFFFu;
    if (table != 0x2A || row == 0 || row > impl_->db->GenericParam.size()) return std::nullopt;
    try {
        auto gp = impl_->db->GenericParam[row - 1];
        GenericParameterInfo info;
        info.Token = genericParamToken;
        info.Number = gp.Number();
        info.Flags = gp.Flags().value;
        info.Name = std::string{ gp.Name() };
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A TypeSpec row's (table 0x1B) raw signature blob (column 0). See the
// header for the full contract.
std::optional<std::vector<std::uint8_t>> MetadataFile::GetTypeSpecSignatureBlob(
        std::uint32_t typeSpecToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = typeSpecToken >> 24;
    std::uint32_t row = typeSpecToken & 0x00FFFFFFu;
    if (table != 0x1B || row == 0 || row > impl_->db->TypeSpec.size()) return std::nullopt;
    try {
        std::uint32_t blobIndex = impl_->db->TypeSpec.get_value<std::uint32_t>(row - 1, 0);
        auto view = impl_->db->get_blob(blobIndex);
        return std::vector<std::uint8_t>(view.begin(), view.end());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A MethodDef (column 4), Field (column 2), or MemberRef (column 2) row's raw
// signature blob. See the header for the full contract.
std::optional<std::vector<std::uint8_t>> MetadataFile::GetSignatureBlob(
        std::uint32_t entityToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = entityToken >> 24;
    std::uint32_t row = entityToken & 0x00FFFFFFu;
    if (row == 0) return std::nullopt;
    try {
        std::uint32_t blobColumn;
        if (table == 0x06 && row <= impl_->db->MethodDef.size()) {
            blobColumn = impl_->db->MethodDef.get_value<std::uint32_t>(row - 1, 4);
        } else if (table == 0x04 && row <= impl_->db->Field.size()) {
            blobColumn = impl_->db->Field.get_value<std::uint32_t>(row - 1, 2);
        } else if (table == 0x0A && row <= impl_->db->MemberRef.size()) {
            blobColumn = impl_->db->MemberRef.get_value<std::uint32_t>(row - 1, 2);
        } else {
            return std::nullopt;
        }
        auto view = impl_->db->get_blob(blobColumn);
        return std::vector<std::uint8_t>(view.begin(), view.end());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A Field row's declaring TypeDef (the C# FieldDefinition.GetDeclaringType()).
// See the header for the full contract.
std::uint32_t MetadataFile::GetFieldDeclaringTypeToken(std::uint32_t fieldToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = fieldToken >> 24;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (table != 0x04 || row == 0 || row > impl_->db->Field.size()) return 0;
    try {
        auto t = impl_->db->Field[row - 1].Parent();
        if (!t) return 0;
        return (0x02u << 24) | ((static_cast<std::uint32_t>(t.index()) + 1) & 0x00FFFFFFu);
    } catch (const std::exception&) {
        return 0;
    }
}

// A GenericParam row's constraint types from the GenericParamConstraint table
// (0x1C): column 0 is the owning GenericParam's 1-based row, column 1 the
// TypeDefOrRef coded index (2-bit tag: 0=TypeDef, 1=TypeRef, 2=TypeSpec).
// See the header for the full contract.
std::vector<std::uint32_t> MetadataFile::GetGenericParameterConstraintTokens(
        std::uint32_t genericParamToken) const {
    std::vector<std::uint32_t> result;
    if (!IsValid()) return result;
    std::uint32_t table = genericParamToken >> 24;
    std::uint32_t row = genericParamToken & 0x00FFFFFFu;
    if (table != 0x2A || row == 0) return result;
    try {
        for (std::uint32_t i = 0; i < impl_->db->GenericParamConstraint.size(); i++) {
            if (impl_->db->GenericParamConstraint.get_value<std::uint32_t>(i, 0) != row)
                continue;
            std::uint32_t v = impl_->db->GenericParamConstraint.get_value<std::uint32_t>(i, 1);
            if (v == 0) continue;
            std::uint32_t tag = v & 0x3u;
            std::uint32_t rid = v >> 2;
            if (rid == 0) continue;
            switch (tag) {
                case 0: result.push_back((0x02u << 24) | rid); break;
                case 1: result.push_back((0x01u << 24) | rid); break;
                case 2: result.push_back((0x1Bu << 24) | rid); break;
            }
        }
    } catch (const std::exception&) {
        // A malformed table walk degrades to the partial result (never throws).
    }
    return result;
}

// A MethodDef row's authored Name. See the header for the full contract.
std::string MetadataFile::GetMethodName(std::uint32_t methodToken) const {
    if (!IsValid()) return {};
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return {};
    try {
        return std::string{ impl_->db->MethodDef[row - 1].Name() };
    } catch (const std::exception&) {
        return {};
    }
}

// A MethodDef row's RVA column. See the header for the full contract.
std::uint32_t MetadataFile::GetMethodRVA(std::uint32_t methodToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return 0;
    try {
        return impl_->db->MethodDef[row - 1].RVA();
    } catch (const std::exception&) {
        return 0;
    }
}

// A Field row's authored Name. See the header for the full contract.
std::string MetadataFile::GetFieldName(std::uint32_t fieldToken) const {
    if (!IsValid()) return {};
    std::uint32_t table = fieldToken >> 24;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (table != 0x04 || row == 0 || row > impl_->db->Field.size()) return {};
    try {
        return std::string{ impl_->db->Field[row - 1].Name() };
    } catch (const std::exception&) {
        return {};
    }
}

// A MemberRef row's Name + MemberRefParent coded index. See the header for
// the full contract.
std::optional<MetadataFile::MemberRefInfo> MetadataFile::GetMemberReference(
        std::uint32_t token) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = token >> 24;
    std::uint32_t row = token & 0x00FFFFFFu;
    if (table != 0x0A || row == 0 || row > impl_->db->MemberRef.size())
        return std::nullopt;
    try {
        MemberRefInfo info;
        info.Token = token;
        info.Name = std::string{ impl_->db->MemberRef[row - 1].Name() };
        // The MemberRefParent coded index (3-bit tag): 0=TypeDef, 1=TypeRef,
        // 2=ModuleRef, 3=MethodDef, 4=TypeSpec.
        std::uint32_t v = impl_->db->MemberRef.get_value<std::uint32_t>(row - 1, 0);
        static constexpr std::uint8_t kParentTables[5] = {0x02, 0x01, 0x1A, 0x06, 0x1B};
        if (v != 0) {
            std::uint32_t tag = v & 0x7u;
            std::uint32_t rid = v >> 3;
            if (rid != 0 && tag < 5)
                info.ParentToken = (static_cast<std::uint32_t>(kParentTables[tag]) << 24) | rid;
        }
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A MethodSpec row's MethodDefOrRef target. See the header for the full
// contract.
std::optional<MetadataFile::MethodSpecInfo> MetadataFile::GetMethodSpecification(
        std::uint32_t methodSpecToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = methodSpecToken >> 24;
    std::uint32_t row = methodSpecToken & 0x00FFFFFFu;
    if (table != 0x2B || row == 0 || row > impl_->db->MethodSpec.size())
        return std::nullopt;
    try {
        // The MethodSpec row has no public column accessors; the raw
        // MethodDefOrRef coded index: bit 0 is the tag (0=MethodDef,
        // 1=MemberRef), the rest the 1-based row (the established read).
        MethodSpecInfo info;
        info.Token = methodSpecToken;
        std::uint32_t v = impl_->db->MethodSpec.get_value<std::uint32_t>(row - 1, 0);
        if (v == 0) return info;  // a nil target; Token still set
        info.MethodToken = ((v & 1) ? 0x0A000000u : 0x06000000u) | (v >> 1);
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A MethodSpec row's raw instantiation blob (column 1). See the header for
// the full contract.
std::optional<std::vector<std::uint8_t>> MetadataFile::GetMethodSpecificationInstantiationBlob(
        std::uint32_t methodSpecToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = methodSpecToken >> 24;
    std::uint32_t row = methodSpecToken & 0x00FFFFFFu;
    if (table != 0x2B || row == 0 || row > impl_->db->MethodSpec.size())
        return std::nullopt;
    try {
        std::uint32_t blobIndex =
            impl_->db->MethodSpec.get_value<std::uint32_t>(row - 1, 1);
        auto view = impl_->db->get_blob(blobIndex);
        return std::vector<std::uint8_t>(view.begin(), view.end());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A StandaloneSig row's raw signature blob (column 0). See the header for
// the full contract.
std::optional<std::vector<std::uint8_t>> MetadataFile::GetStandaloneSignatureBlob(
        std::uint32_t token) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = token >> 24;
    std::uint32_t row = token & 0x00FFFFFFu;
    if (table != 0x11 || row == 0 || row > impl_->db->StandAloneSig.size())
        return std::nullopt;
    try {
        std::uint32_t blobIndex =
            impl_->db->StandAloneSig.get_value<std::uint32_t>(row - 1, 0);
        auto view = impl_->db->get_blob(blobIndex);
        return std::vector<std::uint8_t>(view.begin(), view.end());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A ModuleRef row's authored Name. See the header for the full contract.
std::optional<std::string> MetadataFile::GetModuleReferenceName(std::uint32_t token) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = token >> 24;
    std::uint32_t row = token & 0x00FFFFFFu;
    if (table != 0x1A || row == 0 || row > impl_->db->ModuleRef.size())
        return std::nullopt;
    try {
        return std::string{
            impl_->db->get_string(impl_->db->ModuleRef.get_value<std::uint32_t>(row - 1, 0))
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The whole-table enumerations. See the header for the full contract.
std::vector<MetadataFile::MemberRefInfo> MetadataFile::MemberRefs() const {
    std::vector<MemberRefInfo> result;
    if (!IsValid()) return result;
    try {
        for (std::uint32_t row = 1; row <= impl_->db->MemberRef.size(); row++) {
            MemberRefInfo info;
            info.Token = (0x0Au << 24) | row;
            info.Name = std::string{ impl_->db->MemberRef[row - 1].Name() };
            std::uint32_t v = impl_->db->MemberRef.get_value<std::uint32_t>(row - 1, 0);
            static constexpr std::uint8_t kParentTables[5] = {0x02, 0x01, 0x1A, 0x06, 0x1B};
            if (v != 0) {
                std::uint32_t tag = v & 0x7u;
                std::uint32_t rid = v >> 3;
                if (rid != 0 && tag < 5)
                    info.ParentToken =
                        (static_cast<std::uint32_t>(kParentTables[tag]) << 24) | rid;
            }
            result.push_back(std::move(info));
        }
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

std::vector<MetadataFile::MethodSpecInfo> MetadataFile::MethodSpecs() const {
    std::vector<MethodSpecInfo> result;
    if (!IsValid()) return result;
    try {
        for (std::uint32_t row = 1; row <= impl_->db->MethodSpec.size(); row++) {
            MethodSpecInfo info;
            info.Token = (0x2Bu << 24) | row;
            std::uint32_t v = impl_->db->MethodSpec.get_value<std::uint32_t>(row - 1, 0);
            if (v != 0)
                info.MethodToken = ((v & 1) ? 0x0A000000u : 0x06000000u) | (v >> 1);
            result.push_back(std::move(info));
        }
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

std::vector<std::uint32_t> MetadataFile::StandaloneSignatureTokens() const {
    std::vector<std::uint32_t> result;
    if (!IsValid()) return result;
    for (std::uint32_t row = 1; row <= impl_->db->StandAloneSig.size(); row++) {
        result.push_back((0x11u << 24) | row);
    }
    return result;
}

ILSpy::Decompiler::TypeSystem::ITypePtr MetadataFile::GetFieldSignature(std::uint32_t fieldToken) const {
    if (!IsValid()) return nullptr;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->Field.size()) return nullptr;
    try {
        auto blobIndex = impl_->db->Field.get_value<std::uint32_t>(row - 1, 2);  // Signature blob
        auto blob = impl_->db->get_blob(blobIndex);
        // A field sig's VAR (!N) scopes to the field's declaring TypeDef's
        // GenericParam rows -- resolvable from the FieldDef row alone.
        GenericParamNames genNames;
        {
            auto range = impl_->db->Field[row - 1].Parent().GenericParam();
            for (auto it = range.first; it != range.second; ++it) {
                std::uint32_t n = (*it).Number();
                if (n >= genNames.classNames.size()) genNames.classNames.resize(n + 1);
                genNames.classNames[n] = std::string((*it).Name());
            }
        }
        return DecodeFieldSignatureBlob(*impl_->db, blob.begin(),
                                        static_cast<std::size_t>(blob.end() - blob.begin()),
                                        &genNames);
    } catch (const std::exception&) {
        return nullptr;
    }
}

std::vector<CustomAttributeInfo> MetadataFile::GetCustomAttributes(std::uint32_t entityToken) const {    std::vector<CustomAttributeInfo> result;
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
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    try {
        // The signature blob lives on the MethodDef row (0x06, column 4) or
        // the MemberRef row (0x0A, column 2); a MethodSpec (0x2B) unwraps to
        // the underlying definition's signature (the instantiation only binds
        // generic arguments, which do not change the parameter list). Blobs
        // decode with the hand-rolled ECMA-335 parser (SignatureDecoder.cpp):
        // winmd's TypeSig window rejects TypedByRef / sentinel / fn-ptr /
        // custom modifiers that framework signatures use.
        std::uint32_t blobColumn;
        if (table == 0x06 && row && row <= impl_->db->MethodDef.size()) {
            blobColumn = impl_->db->MethodDef.get_value<std::uint32_t>(row - 1, 4);
        } else if (table == 0x0A && row && row <= impl_->db->MemberRef.size()) {
            blobColumn = impl_->db->MemberRef.get_value<std::uint32_t>(row - 1, 2);
        } else if (table == 0x2B && row && row <= impl_->db->MethodSpec.size()) {
            // winmd's MethodSpec row has no public column accessors; read the
            // MethodDefOrRef coded index (column 0) raw: bit 0 is the tag
            // (0=MethodDef, 1=MemberRef), the rest is the 1-based row.
            std::uint32_t v = impl_->db->MethodSpec.get_value<std::uint32_t>(row - 1, 0);
            if (v == 0) return std::nullopt;
            return GetMethodSignature(((v & 1) ? 0x0A000000u : 0x06000000u) | (v >> 1));
        } else {
            return std::nullopt;
        }
        auto blob = impl_->db->get_blob(blobColumn);
        bool ok = false;
        // Authors' generic parameter names: MethodDef.GenericParam rows name
        // this method's MVAR (!!N) params; the declaring TypeDef's GenericParam
        // rows name the class's VAR (!N) params. Lookups are 0-based via the
        // GenericParam.Number column; rows need not be table-ordered.
        GenericParamNames genNames;
        const GenericParamNames* genNamesPtr = nullptr;
        if (table == 0x06) {  // MethodDef row has both lists; MemberRef has none.
            auto md = impl_->db->MethodDef[row - 1];
            auto collectInto = [](auto range, std::vector<std::string>& out) {
                for (auto it = range.first; it != range.second; ++it) {
                    std::uint32_t n = (*it).Number();
                    if (n >= out.size()) out.resize(n + 1);
                    out[n] = std::string((*it).Name());
                }
            };
            collectInto(md.GenericParam(), genNames.methodNames);
            auto td = md.Parent();  // declaring TypeDef
            collectInto(td.GenericParam(), genNames.classNames);
            genNamesPtr = &genNames;
        }
        DecodedMethodSignature d = DecodeMethodSignatureBlob(
            *impl_->db, blob.begin(), static_cast<std::size_t>(blob.end() - blob.begin()), ok,
            genNamesPtr);
        if (!ok) return std::nullopt;
        MethodSignature out;
        out.ReturnType = std::move(d.ReturnType);
        out.ParameterTypes = std::move(d.ParameterTypes);
        out.IsInstance = d.IsInstance;
        out.GenericParameterCount = d.GenericParameterCount;
        return out;
    } catch (const std::exception&) {
        // Malformed row/bind data: degrade to std::nullopt rather than throwing,
        // matching the decompiler's robustness tenet.
        return std::nullopt;
    }
}

namespace {
// "Namespace.Name" form for a type, or just Name when there is no namespace.
std::string TypeNameStr(std::string_view ns, std::string_view name) {
    if (ns.empty()) return std::string(name);
    std::string r(ns);
    r += '.';
    r += name;
    return r;
}
} // namespace

std::string MetadataFile::ResolveTokenToString(std::uint32_t token,
                                               std::uint32_t ownerMethodToken) const {
    if (!IsValid()) return {};
    std::uint32_t table = token >> 24;
    std::uint32_t row = token & 0x00FFFFFFu;
    auto fallback = [&] {
        // Raw hex token, e.g. "0x06000007". Used for TypeSpec/StandAloneSig/
        // MethodSpec and any out-of-range or unsupported kind.
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", token);
        return std::string(buf);
    };
    try {
        if (table == 0x70) {  // UserString (#US heap)
            auto s = GetUserString(token);
            return s.empty() ? fallback() : s;
        }
        if (table == 0x01 && row && row <= impl_->db->TypeRef.size()) {  // TypeRef
            auto r = impl_->db->TypeRef[row - 1];
            return TypeNameStr(r.TypeNamespace(), r.TypeName());
        }
        if (table == 0x02 && row && row <= impl_->db->TypeDef.size()) {  // TypeDef
            auto r = impl_->db->TypeDef[row - 1];
            return TypeNameStr(r.TypeNamespace(), r.TypeName());
        }
        if (table == 0x04 && row && row <= impl_->db->Field.size()) {  // Field
            auto f = impl_->db->Field[row - 1];
            auto p = f.Parent();
            return TypeNameStr(p.TypeNamespace(), p.TypeName()) + "::" + std::string(f.Name());
        }
        if (table == 0x06 && row && row <= impl_->db->MethodDef.size()) {  // MethodDef
            auto m = impl_->db->MethodDef[row - 1];
            auto p = m.Parent();
            return TypeNameStr(p.TypeNamespace(), p.TypeName()) + "::" + std::string(m.Name());
        }
        if (table == 0x0A && row && row <= impl_->db->MemberRef.size()) {  // MemberRef
            auto mr = impl_->db->MemberRef[row - 1];
            auto parent = mr.Class();
            using MRP = winmd::reader::MemberRefParent;
            if (parent.type() == MRP::TypeRef) {
                auto t = parent.TypeRef();
                return TypeNameStr(t.TypeNamespace(), t.TypeName()) + "::" + std::string(mr.Name());
            }
            if (parent.type() == MRP::TypeDef) {
                auto t = parent.TypeDef();
                return TypeNameStr(t.TypeNamespace(), t.TypeName()) + "::" + std::string(mr.Name());
            }
            if (parent.type() == MRP::TypeSpec) {
                // A generic instantiation: resolve the TypeSpec's signature to a
                // display name (e.g. "System.Collections.ObjectModel.ReadOnlyCollection`1").
                // VAR/MVAR in the TypeSpec bind in the CALLING method's scope.
                GenericParamNames genNames;
                const GenericParamNames* genNamesPtr = nullptr;
                if ((ownerMethodToken >> 24) == 0x06) {
                    genNames = BuildGenericParamNames(*impl_->db, ownerMethodToken & 0x00FFFFFFu);
                    genNamesPtr = &genNames;
                }
                auto ts = parent.get_row<winmd::reader::TypeSpec>();
                std::uint32_t blobIndex = ts.get_value<std::uint32_t>(0);
                auto blob = impl_->db->get_blob(blobIndex);
                auto type = DecodeTypeSpecBlob(*impl_->db, blob.begin(),
                    static_cast<std::size_t>(blob.end() - blob.begin()), genNamesPtr);
                std::string tn = type ? type->ReflectionName() : std::string("?");
                return tn + "::" + std::string(mr.Name());
            }
            // ModuleRef/MethodDef parent: best-effort, member name only.
            return std::string(mr.Name());
        }
        if (table == 0x2B && row && row <= impl_->db->MethodSpec.size()) {
            // MethodSpec: unwrap to the underlying MethodDefOrRef coded index
            // (column 0) and resolve that. winmd's MethodSpec row has no public
            // accessors; read the raw value: bit 0 is the tag (0=MethodDef,
            // 1=MemberRef), the rest is the 1-based row. So a generic-instantiation
            // call (e.g. `Activator.CreateInstance<T>()`) renders its resolved
            // method name, not the raw token.
            std::uint32_t v = impl_->db->MethodSpec.get_value<std::uint32_t>(row - 1, 0);
            if (v != 0)
                return ResolveTokenToString(((v & 1) ? 0x0A000000u : 0x06000000u) | (v >> 1),
                                            ownerMethodToken);
        }
    } catch (const std::exception&) {
        // Fall through to the raw-token fallback.
    }
    return fallback();
}

ILSpy::Decompiler::TypeSystem::ITypePtr MetadataFile::ResolveTypeToken(std::uint32_t token,
                                                                       std::uint32_t ownerMethodToken) const {
    if (!IsValid()) return nullptr;
    std::uint32_t table = token >> 24;
    std::uint32_t row = token & 0x00FFFFFFu;
    try {
        if (table == 0x01 && row && row <= impl_->db->TypeRef.size()) {  // TypeRef
            auto r = impl_->db->TypeRef[row - 1];
            return MakeTypeRef(r.TypeNamespace(), r.TypeName(), 0);
        }
        if (table == 0x02 && row && row <= impl_->db->TypeDef.size()) {  // TypeDef
            auto r = impl_->db->TypeDef[row - 1];
            return MakeTypeRef(r.TypeNamespace(), r.TypeName(), 0);
        }
        if (table == 0x1B && row && row <= impl_->db->TypeSpec.size()) {  // TypeSpec
            // Column 0 is the signature blob; decode the content type
            // (SZArray / multi-dim array / generic instantiation / ptr & byref).
            // VAR/MVAR in the blob scope to the owning method body.
            GenericParamNames genNames;
            const GenericParamNames* genNamesPtr = nullptr;
            if ((ownerMethodToken >> 24) == 0x06) {
                genNames = BuildGenericParamNames(*impl_->db, ownerMethodToken & 0x00FFFFFFu);
                genNamesPtr = &genNames;
            }
            auto blobIndex = impl_->db->TypeSpec.get_value<std::uint32_t>(row - 1, 0);
            auto blob = impl_->db->get_blob(blobIndex);
            return DecodeTypeSpecBlob(*impl_->db, blob.begin(),
                                      static_cast<std::size_t>(blob.end() - blob.begin()),
                                      genNamesPtr);
        }
    } catch (const std::exception&) {
        return nullptr;
    }
    return nullptr;
}

ILSpy::Decompiler::TypeSystem::ITypePtr MetadataFile::ResolveMethodDeclaringType(std::uint32_t methodToken,
                                                                                 std::uint32_t ownerMethodToken) const {
    if (!IsValid()) return nullptr;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    try {
        if (table == 0x06 && row && row <= impl_->db->MethodDef.size()) {
            // MethodDef: parent is its enclosing TypeDef. Derive the TypeKind
            // from the TypeDef row (flags + base) so a delegate constructor's
            // declaring type resolves to Kind == Delegate, etc.
            auto m = impl_->db->MethodDef[row - 1];
            auto p = m.Parent();
            return MakeTypeRefFromTypeDef(p);
        }
        if (table == 0x0A && row && row <= impl_->db->MemberRef.size()) {
            // MemberRef: parent is a TypeRef / TypeDef / TypeSpec (or a
            // ModuleRef/MethodDef, which carry no declaring type here).
            auto mr = impl_->db->MemberRef[row - 1];
            auto parent = mr.Class();
            using MRP = winmd::reader::MemberRefParent;
            if (parent.type() == MRP::TypeRef) {
                auto t = parent.TypeRef();
                return MakeTypeRefFromTypeRef(t);
            }
            if (parent.type() == MRP::TypeDef) {
                auto t = parent.TypeDef();
                return MakeTypeRefFromTypeDef(t);
            }
            if (parent.type() == MRP::TypeSpec) {
                // The TypeSpec (e.g. `List<!0>` or `ArraySortHelper<!!0>`) is
                // instantiated in the CALLER's scope: its VAR/MVAR bind to the
                // calling method body's class/method generic params.
                GenericParamNames genNames;
                const GenericParamNames* genNamesPtr = nullptr;
                if ((ownerMethodToken >> 24) == 0x06) {
                    genNames = BuildGenericParamNames(*impl_->db, ownerMethodToken & 0x00FFFFFFu);
                    genNamesPtr = &genNames;
                }
                auto ts = parent.get_row<winmd::reader::TypeSpec>();
                std::uint32_t blobIndex = ts.get_value<std::uint32_t>(0);
                auto blob = impl_->db->get_blob(blobIndex);
                return DecodeTypeSpecBlob(*impl_->db, blob.begin(),
                                          static_cast<std::size_t>(blob.end() - blob.begin()),
                                          genNamesPtr);
            }
            return nullptr;
        }
        if (table == 0x2B && row && row <= impl_->db->MethodSpec.size()) {
            // MethodSpec: unwrap to the underlying MethodDefOrRef coded index
            // (column 0). winmd's MethodSpec row has no public accessors; read
            // the raw value: bit 0 is the tag (0=MethodDef, 1=MemberRef), the
            // rest is the 1-based row.
            std::uint32_t v = impl_->db->MethodSpec.get_value<std::uint32_t>(row - 1, 0);
            if (v == 0) return nullptr;
            return ResolveMethodDeclaringType(((v & 1) ? 0x0A000000u : 0x06000000u) | (v >> 1));
        }
    } catch (const std::exception&) {
        return nullptr;
    }
    return nullptr;
}

int MetadataFile::GetMethodSpecTypeArgumentCount(std::uint32_t methodToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    try {
        if (table == 0x2B && row && row <= impl_->db->MethodSpec.size()) {
            // Column 1 is the Instantiation blob (the MethodSpecSig). Read it
            // and decode the generic-argument count. A non-MethodSpec token, an
            // out-of-range row, or a malformed/missing blob returns 0.
            std::uint32_t blobIndex = impl_->db->MethodSpec.get_value<std::uint32_t>(row - 1, 1);
            auto blob = impl_->db->get_blob(blobIndex);
            int count = DecodeMethodSpecTypeArgCount(
                *impl_->db, blob.begin(),
                static_cast<std::size_t>(blob.end() - blob.begin()));
            return count < 0 ? 0 : count;
        }
    } catch (const std::exception&) {
        return 0;
    }
    return 0;
}

namespace {
// Whether a custom-attribute range contains [CompilerGenerated] (System.Runtime.
// CompilerServices.CompilerGeneratedAttribute). Mirrors the C#
// HasKnownAttribute(KnownAttribute.CompilerGenerated) check that NRExtensions /
// SRMExtensions.IsCompilerGenerated use. The range is a winmd equal_range pair
// (CustomAttribute rows for one entity); the row type IS the iterator
// (row_base is a random-access iterator), so the pair is std::pair<Row, Row>.
template <typename Range>
bool RangeHasCompilerGenerated(const Range& range) {
    for (auto it = range.first; it != range.second; ++it) {
        try {
            auto ns_name = (*it).TypeNamespaceAndName();
            if (std::string_view(ns_name.first) == "System.Runtime.CompilerServices"
                && std::string_view(ns_name.second) == "CompilerGeneratedAttribute")
                return true;
        } catch (const std::exception&) {
            // Skip an attribute whose type name cannot be resolved.
        }
    }
    return false;
}
} // namespace

// Whether a field token is compiler-generated or in a compiler-generated class.
// See the header for the full contract. The field's own [CompilerGenerated] or
// (recursively up the nesting chain) its declaring type's, mirroring the C#
// NRExtensions.IsCompilerGeneratedOrIsInCompilerGeneratedClass. A depth guard
// stops a malformed cyclic NestedClass chain from looping forever.
bool MetadataFile::IsFieldCompilerGeneratedOrInCompilerGeneratedClass(std::uint32_t fieldToken) const {
    if (!IsValid()) return false;
    std::uint32_t table = fieldToken >> 24;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    // Walk a TypeDef's nesting chain checking each type's [CompilerGenerated].
    // winmd's row_base::index() is 0-based, so the 1-based token RID is index()+1.
    auto typeChainIsCG = [](winmd::reader::TypeDef t) -> bool {
        for (int depth = 0; depth < 64 && t; ++depth) {
            if (RangeHasCompilerGenerated(t.CustomAttribute())) return true;
            t = t.EnclosingType();
        }
        return false;
    };
    try {
        if (table == 0x04 && row && row <= impl_->db->Field.size()) {
            // FieldDef: the field's own [CompilerGenerated], then its declaring
            // type's (up the nesting chain).
            if (RangeHasCompilerGenerated(impl_->db->Field[row - 1].CustomAttribute())) return true;
            return typeChainIsCG(impl_->db->Field[row - 1].Parent());
        }
        if (table == 0x0A && row && row <= impl_->db->MemberRef.size()) {
            // MemberRef field: only an in-module TypeDef parent can be checked
            // here (a cross-assembly TypeRef needs the full type system).
            auto mr = impl_->db->MemberRef[row - 1];
            auto parent = mr.Class();
            using MRP = winmd::reader::MemberRefParent;
            if (parent.type() == MRP::TypeDef) {
                return typeChainIsCG(parent.TypeDef());
            }
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

MethodDefKindInfo MetadataFile::GetMethodDefKindInfo(std::uint32_t methodToken) const {
    MethodDefKindInfo info;
    if (!IsValid()) return info;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return info;
    try {
        auto m = impl_->db->MethodDef[row - 1];
        auto flags = m.Flags();
        // Faithful to MetadataMethod's SymbolKind == Constructor gate: a name
        // of .ctor/.cctor with the SpecialName|RTSpecialName flag. (C#-compiled
        // constructors always carry RTSpecialName, so the flag check is the
        // faithful guard; the name distinguishes the two .ctor forms.)
        if (flags.SpecialName() || flags.RTSpecialName()) {
            std::string name{ m.Name() };
            if (name == ".ctor" || name == ".cctor") {
                info.IsConstructor = true;
            }
        }
        info.IsStatic = flags.Static();
    } catch (const std::exception&) {
        // Best-effort: a malformed MethodDef row leaves the defaults.
    }
    return info;
}

std::vector<std::string> MetadataFile::GetParameterNames(std::uint32_t methodToken) const {
    std::vector<std::string> result;
    if (!IsValid()) return result;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return result;
    try {
        auto m = impl_->db->MethodDef[row - 1];
        // ParamList is a (first, last) iterator pair into the Param table. A
        // row with Sequence 0 describes the return value; rows with Sequence
        // >= 1 name the declared parameters (the implicit `this` is absent).
        auto range = m.ParamList();
        for (auto it = range.first; it != range.second; ++it) {
            auto p = *it;
            std::uint16_t seq = p.Sequence();
            if (seq == 0) continue;
            std::string name{ p.Name() };
            if (name.empty()) continue;
            if (result.size() < seq) result.resize(seq);
            result[seq - 1] = std::move(name);
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed Param row leaves the result short.
    }
    return result;
}

std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> MetadataFile::GetLocalTypes(std::uint32_t localVarSigToken,
                                                                                 std::uint32_t ownerMethodToken) const {
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> result;
    if (!IsValid() || localVarSigToken == 0) return result;
    std::uint32_t table = localVarSigToken >> 24;
    std::uint32_t row = localVarSigToken & 0x00FFFFFFu;
    if (table != 0x11 || row == 0 || row > impl_->db->StandAloneSig.size()) return result;
    try {
        std::uint32_t blobColumn = impl_->db->StandAloneSig.get_value<std::uint32_t>(row - 1, 0);
        auto blob = impl_->db->get_blob(blobColumn);
        // A local sig's VAR/MVAR scope to the owning method body's class /
        // method generic params; the IL reader passes the method token so
        // generic-typed locals come back authored-named (T, ...).
        GenericParamNames genNames;
        const GenericParamNames* genNamesPtr = nullptr;
        if ((ownerMethodToken >> 24) == 0x06) {
            genNames = BuildGenericParamNames(*impl_->db, ownerMethodToken & 0x00FFFFFFu);
            genNamesPtr = &genNames;
        }
        auto infos = DecodeLocalSignatureBlob(
            *impl_->db, blob.begin(), static_cast<std::size_t>(blob.end() - blob.begin()),
            genNamesPtr);
        result.reserve(infos.size());
        for (auto& info : infos) result.push_back(std::move(info.Type));
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

std::vector<ILSpy::Decompiler::Metadata::LocalTypeInfo> MetadataFile::GetLocalTypesWithPinned(std::uint32_t localVarSigToken,
                                                                                              std::uint32_t ownerMethodToken) const {
    std::vector<LocalTypeInfo> result;
    if (!IsValid() || localVarSigToken == 0) return result;
    std::uint32_t table = localVarSigToken >> 24;
    std::uint32_t row = localVarSigToken & 0x00FFFFFFu;
    if (table != 0x11 || row == 0 || row > impl_->db->StandAloneSig.size()) return result;
    try {
        std::uint32_t blobColumn = impl_->db->StandAloneSig.get_value<std::uint32_t>(row - 1, 0);
        auto blob = impl_->db->get_blob(blobColumn);
        GenericParamNames genNames;
        const GenericParamNames* genNamesPtr = nullptr;
        if ((ownerMethodToken >> 24) == 0x06) {
            genNames = BuildGenericParamNames(*impl_->db, ownerMethodToken & 0x00FFFFFFu);
            genNamesPtr = &genNames;
        }
        result = DecodeLocalSignatureBlob(
            *impl_->db, blob.begin(), static_cast<std::size_t>(blob.end() - blob.begin()),
            genNamesPtr);
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

std::string MetadataFile::GetUserString(std::uint32_t token) const {
    if (!IsValid() || !impl_->bodyReader) return {};
    return impl_->bodyReader->GetUserString(token);
}

// The null-vs-valid-empty distinction. See the header for the full contract.
std::optional<std::string> MetadataFile::TryGetUserString(std::uint32_t token) const {
    if (!IsValid() || !impl_->bodyReader) return std::nullopt;
    return impl_->bodyReader->TryGetUserString(token);
}

// The cor20 entrypoint token. See the header for the full contract.
std::uint32_t MetadataFile::GetEntryPointToken() const {
    if (!IsValid() || !impl_->bodyReader) return 0;
    return impl_->bodyReader->EntryPointToken();
}

} // namespace ILSpy::Decompiler::Metadata
