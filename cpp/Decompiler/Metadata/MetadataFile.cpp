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
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/Metadata/MethodBodyReader.hpp"
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Metadata/SignatureDecoder.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TypeKindDerivation.hpp"

#include "Decompiler/Metadata/Ecma335/WinmdInclude.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
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

    // The GetTypeDefinition / GetTypeForwarder reverse-lookup caches (the
    // C# LazyInit dictionaries, built on first use -- the port is
    // single-threaded, so a plain built-flag gate is the VolatileRead/
    // GetOrSet pair's behavioral equivalent). The name keys are ordered
    // maps over the value types' structural fields (the C# Dictionary
    // hash is an implementation detail; only membership/lookup order at
    // the key level is observable, and both containers agree on that).
    struct TopLevelTypeNameOrder {
        bool operator()(const TypeSystem::TopLevelTypeName& a,
                        const TypeSystem::TopLevelTypeName& b) const {
            if (a.Namespace() != b.Namespace()) return a.Namespace() < b.Namespace();
            if (a.Name() != b.Name()) return a.Name() < b.Name();
            return a.TypeParameterCount() < b.TypeParameterCount();
        }
    };
    struct FullTypeNameOrder {
        bool operator()(const TypeSystem::FullTypeName& a,
                        const TypeSystem::FullTypeName& b) const {
            const auto& ta = a.GetTopLevelTypeName();
            const auto& tb = b.GetTopLevelTypeName();
            TopLevelTypeNameOrder topLevelOrder;
            if (topLevelOrder(ta, tb)) return true;
            if (topLevelOrder(tb, ta)) return false;
            int n = std::min(a.NestingLevel(), b.NestingLevel());
            for (int i = 0; i < n; i++) {
                std::string na = a.GetNestedTypeName(i);
                std::string nb = b.GetNestedTypeName(i);
                if (na != nb) return na < nb;
                int ca = a.GetNestedTypeAdditionalTypeParameterCount(i);
                int cb = b.GetNestedTypeAdditionalTypeParameterCount(i);
                if (ca != cb) return ca < cb;
            }
            return a.NestingLevel() < b.NestingLevel();
        }
    };
    std::map<TypeSystem::TopLevelTypeName, std::uint32_t,
             TopLevelTypeNameOrder> typeLookup;
    bool typeLookupBuilt = false;
    std::map<TypeSystem::FullTypeName, std::uint32_t,
             FullTypeNameOrder> typeForwarderLookup;
    bool typeForwarderLookupBuilt = false;

    // The namespace-definition cache (the MetadataReader's NamespaceCache;
    // NamespaceDefinition.hpp documents the lazy build).
    std::unique_ptr<NamespaceCache> namespaceCache;

    // The lazily-created cache (the MetadataFile const accessors reach the
    // pimpl through a const unique_ptr, so the pointee's mutability suffices;
    // the owner file is the caller -- the cache reads its raw tables).
    const NamespaceCache& Namespaces(const MetadataFile* owner) {
        if (!namespaceCache)
            namespaceCache = std::make_unique<NamespaceCache>(owner);
        return *namespaceCache;
    }

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

const std::string& MetadataFile::FileName() const noexcept {
    return impl_->path;
}

std::string MetadataFile::Name() const {
    // The C# MetadataFile.Name: the Assembly table Name when the file is an
    // assembly manifest (metadata.IsAssembly), else the Module table Name
    // (a netmodule). The debug-metadata third arm ("debug metadata") is
    // n/a -- the port's reader never constructs the metadata-only shape;
    // an invalid file yields "" (the never-throws contract).
    if (std::optional<AssemblyDefinitionInfo> assembly = GetAssemblyDefinition())
        return assembly->Name;
    if (std::optional<ModuleDefinitionInfo> module = GetModuleDefinition())
        return module->Name;
    return {};
}

std::string MetadataFile::FullName() const {
    // The C# MetadataFile.FullName (MetadataFile.cs): the Assembly table's
    // full display name for an assembly manifest, else Name. The IsAssembly
    // test reads the raw Assembly row count (the C# metadata.IsAssembly), so
    // a corrupt Assembly row still takes the assembly arm and propagates
    // the raw-surface throw exactly as the C# propagates
    // BadImageFormatException.
    if (CorTableRowCount(CorTableIndex::Assembly) > 0)
        return GetFullAssemblyName(*this);
    return Name();
}

std::uint32_t MetadataFile::TypeDefCount() const noexcept {
    return IsValid() ? impl_->db->TypeDef.size() : 0;
}

std::uint32_t MetadataFile::FieldCount() const noexcept {
    return IsValid() ? impl_->db->Field.size() : 0;
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

// An InterfaceImpl row's Interface column and a TypeDef row's Extends
// column (both TypeDefOrRef coded indexes, 2 tag bits) as a raw token:
// TypeDef tag 0 -> 0x02, TypeRef tag 1 -> 0x01, TypeSpec tag 2 -> 0x1B;
// a raw 0 column is the nil handle.
static std::uint32_t TypeDefOrRefColumnToToken(std::uint32_t raw) {
    if (raw == 0) return 0;
    std::uint32_t rid = raw >> 2;
    switch (raw & 0x3u) {
        case 0: return (0x02u << 24) | rid;
        case 1: return (0x01u << 24) | rid;
        case 2: return (0x1Bu << 24) | rid;
        default: return 0;
    }
}

std::vector<MetadataFile::InterfaceImplementationInfo>
MetadataFile::GetInterfaceImplementations(std::uint32_t typeDefToken) const {
    std::vector<InterfaceImplementationInfo> result;
    if (!IsValid()) return result;
    std::uint32_t table = typeDefToken >> 24;
    std::uint32_t row = typeDefToken & 0x00FFFFFFu;
    if (table != 0x02 || row == 0 || row > impl_->db->TypeDef.size())
        return result;
    try {
        auto t = impl_->db->TypeDef[row - 1];
        auto range = t.InterfaceImpl();
        for (auto it = range.first; it != range.second; ++it) {
            InterfaceImplementationInfo info;
            info.Token = (0x09u << 24)
                | ((static_cast<std::uint32_t>((*it).index()) + 1) & 0x00FFFFFFu);
            info.InterfaceToken =
                TypeDefOrRefColumnToToken((*it).get_value<std::uint32_t>(1));
            result.push_back(info);
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed table walk degrades to the partial result.
    }
    return result;
}

std::optional<MetadataFile::InterfaceImplementationInfo>
MetadataFile::GetInterfaceImplementation(std::uint32_t implToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = implToken >> 24;
    std::uint32_t row = implToken & 0x00FFFFFFu;
    if (table != 0x09 || row == 0 || row > impl_->db->InterfaceImpl.size())
        return std::nullopt;
    try {
        InterfaceImplementationInfo info;
        info.Token = implToken;
        info.InterfaceToken =
            TypeDefOrRefColumnToToken(
                impl_->db->InterfaceImpl.get_value<std::uint32_t>(row - 1, 1));
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A Property row's Name (the GetProperties row read, by the row's own
// token). See the header for the contract.
std::string MetadataFile::GetPropertyName(std::uint32_t propertyToken) const {
    if (!IsValid()) return {};
    std::uint32_t table = propertyToken >> 24;
    std::uint32_t row = propertyToken & 0x00FFFFFFu;
    if (table != 0x17 || row == 0 || row > impl_->db->Property.size()) return {};
    try {
        return std::string(impl_->db->Property[row - 1].Name());
    } catch (const std::exception&) {
        return {};
    }
}

// An Event row's Name (same shape as GetPropertyName).
std::string MetadataFile::GetEventName(std::uint32_t eventToken) const {
    if (!IsValid()) return {};
    std::uint32_t table = eventToken >> 24;
    std::uint32_t row = eventToken & 0x00FFFFFFu;
    if (table != 0x14 || row == 0 || row > impl_->db->Event.size()) return {};
    try {
        return std::string(impl_->db->Event[row - 1].Name());
    } catch (const std::exception&) {
        return {};
    }
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

// A TypeDef row's Extends column as a raw token (the C# ILSpy
// SRMExtensions `TypeDefinition.GetBaseTypeOrNil()`). See the header for
// the full contract.
std::uint32_t MetadataFile::GetBaseTypeToken(std::uint32_t typeToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = typeToken >> 24;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (table != 0x02 || row == 0 || row > impl_->db->TypeDef.size()) return 0;
    try {
        // The Extends column is a TypeDefOrRef coded index (2 tag bits).
        return TypeDefOrRefColumnToToken(
            impl_->db->TypeDef[row - 1].get_value<std::uint32_t>(3));
    } catch (const std::exception&) {
        // Malformed image: report the nil base.
        return 0;
    }
}

// The nested TypeDef tokens of a TypeDef (the C#
// `TypeDefinition.GetNestedTypes()`). See the header for the full contract.
std::vector<std::uint32_t> MetadataFile::GetNestedTypes(
    std::uint32_t typeToken) const {
    std::vector<std::uint32_t> result;
    if (!IsValid()) return result;
    std::uint32_t table = typeToken >> 24;
    std::uint32_t row = typeToken & 0x00FFFFFFu;
    if (table != 0x02 || row == 0 || row > impl_->db->TypeDef.size()) return result;
    try {
        // The NestedClass table in row order: each row's EnclosingClass
        // names the parent, its NestedClass the nested TypeDef (the same
        // rows the SRM GetNestedTypes collection walks).
        for (auto&& nc : impl_->db->NestedClass) {
            if (static_cast<std::uint32_t>(nc.EnclosingType().index()) + 1 != row)
                continue;
            std::uint32_t nestedRow =
                static_cast<std::uint32_t>(nc.NestedType().index()) + 1;
            result.push_back((0x02u << 24) | (nestedRow & 0x00FFFFFFu));
        }
    } catch (const std::exception&) {
        // Malformed image: report no nested types.
    }
    return result;
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
        } else if (table == 0x17 && row <= impl_->db->Property.size()) {
            blobColumn = impl_->db->Property.get_value<std::uint32_t>(row - 1, 2);
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

// The GenericParamConstraint rows (the C# GenericParameter.GetConstraints()
// collection): the row's own token plus its constraint Type. See the header
// for the full contract.
std::vector<GenericParamConstraintInfo> MetadataFile::GetGenericParameterConstraints(
        std::uint32_t genericParamToken) const {
    std::vector<GenericParamConstraintInfo> result;
    if (!IsValid()) return result;
    std::uint32_t table = genericParamToken >> 24;
    std::uint32_t row = genericParamToken & 0x00FFFFFFu;
    if (table != 0x2A || row == 0) return result;
    try {
        for (std::uint32_t i = 0; i < impl_->db->GenericParamConstraint.size(); i++) {
            if (impl_->db->GenericParamConstraint.get_value<std::uint32_t>(i, 0) != row)
                continue;
            GenericParamConstraintInfo info;
            info.Token = (0x2Cu << 24) | ((i + 1) & 0x00FFFFFFu);
            std::uint32_t v = impl_->db->GenericParamConstraint.get_value<std::uint32_t>(i, 1);
            if (v != 0) {
                std::uint32_t tag = v & 0x3u;
                std::uint32_t rid = v >> 2;
                if (rid != 0) {
                    switch (tag) {
                        case 0: info.TypeToken = (0x02u << 24) | rid; break;
                        case 1: info.TypeToken = (0x01u << 24) | rid; break;
                        case 2: info.TypeToken = (0x1Bu << 24) | rid; break;
                    }
                }
            }
            result.push_back(info);
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

// A Field row's RelativeVirtualAddress (the FieldRVA table row whose Field
// is this row -- the C# FindFieldRvaRowId scan). See the header for the full
// contract.
std::uint32_t MetadataFile::GetFieldRVA(std::uint32_t fieldToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = fieldToken >> 24;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (table != 0x04 || row == 0 || row > impl_->db->Field.size()) return 0;
    try {
        auto& db = *impl_->db;
        // The Field column is a plain 1-based Field row index (the winmd
        // MethodImpl Class-column convention).
        for (std::uint32_t i = 0; i < db.FieldRVA.size(); i++) {
            if (db.FieldRVA.get_value<std::uint32_t>(i, 1) != row) continue;
            return db.FieldRVA.get_value<std::uint32_t>(i, 0);
        }
    } catch (const std::exception&) {
        // Malformed image: report no RVA (the field renders without data).
    }
    return 0;
}

// A Field row's layout offset (the FieldLayout table row whose Field is this
// row; -1 when none). See the header for the full contract.
std::int32_t MetadataFile::GetFieldOffset(std::uint32_t fieldToken) const {
    if (!IsValid()) return -1;
    std::uint32_t table = fieldToken >> 24;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (table != 0x04 || row == 0 || row > impl_->db->Field.size()) return -1;
    try {
        auto& db = *impl_->db;
        for (std::uint32_t i = 0; i < db.FieldLayout.size(); i++) {
            if (db.FieldLayout.get_value<std::uint32_t>(i, 1) != row) continue;
            // The Offset column is a 4-byte uint; the > int.MaxValue shape
            // reads -1 (the C# GetOffset clamp -- never a real layout).
            std::uint32_t offset = db.FieldLayout.get_value<std::uint32_t>(i, 0);
            if (offset > 0x7FFFFFFFu) return -1;
            return static_cast<std::int32_t>(offset);
        }
    } catch (const std::exception&) {
        // Malformed image: report no layout offset.
    }
    return -1;
}

// A Field row's marshalling-descriptor blob (the FieldMarshal row whose
// HasFieldMarshal coded index -- Field tag 0, Param tag 1 -- points at this
// row). See the header for the full contract.
std::optional<std::vector<std::uint8_t>>
MetadataFile::GetFieldMarshallingDescriptor(std::uint32_t fieldToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = fieldToken >> 24;
    std::uint32_t row = fieldToken & 0x00FFFFFFu;
    if (table != 0x04 || row == 0 || row > impl_->db->Field.size())
        return std::nullopt;
    try {
        auto& db = *impl_->db;
        std::uint32_t want = (row << 1) | 0;  // HasFieldMarshal: Field tag 0
        for (std::uint32_t i = 0; i < db.FieldMarshal.size(); i++) {
            if (db.FieldMarshal.get_value<std::uint32_t>(i, 0) != want)
                continue;
            auto blob = db.get_blob(
                db.FieldMarshal.get_value<std::uint32_t>(i, 1));
            return std::vector<std::uint8_t>(blob.begin(), blob.end());
        }
    } catch (const std::exception&) {
        // Malformed image: report no descriptor.
    }
    return std::nullopt;
}

// A TypeDef row's ClassLayout ClassSize (0 when the row has no ClassLayout
// row). See the header for the full contract.
std::uint32_t MetadataFile::GetTypeLayoutSize(std::uint32_t typeDefToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = typeDefToken >> 24;
    std::uint32_t row = typeDefToken & 0x00FFFFFFu;
    if (table != 0x02 || row == 0 || row > impl_->db->TypeDef.size()) return 0;
    try {
        auto& db = *impl_->db;
        // The Parent column is a plain 1-based TypeDef row index.
        for (std::uint32_t i = 0; i < db.ClassLayout.size(); i++) {
            if (db.ClassLayout.get_value<std::uint32_t>(i, 2) != row) continue;
            return db.ClassLayout.get_value<std::uint32_t>(i, 1);
        }
    } catch (const std::exception&) {
        // Malformed image: report no layout size.
    }
    return 0;
}

// A TypeDef row's layout (the C# `TypeDefinition.GetLayout()` over its
// ClassLayout row). See the header for the full contract.
MetadataFile::TypeLayoutInfo MetadataFile::GetTypeLayout(
    std::uint32_t typeDefToken) const {
    TypeLayoutInfo result;
    if (!IsValid()) return result;
    std::uint32_t table = typeDefToken >> 24;
    std::uint32_t row = typeDefToken & 0x00FFFFFFu;
    if (table != 0x02 || row == 0 || row > impl_->db->TypeDef.size()) return result;
    try {
        auto& db = *impl_->db;
        // The Parent column is a plain 1-based TypeDef row index; column 0
        // is PackingSize (uint16), column 1 ClassSize (uint32).
        for (std::uint32_t i = 0; i < db.ClassLayout.size(); i++) {
            if (db.ClassLayout.get_value<std::uint32_t>(i, 2) != row) continue;
            result.PackingSize =
                db.ClassLayout.get_value<std::uint16_t>(i, 0);
            result.ClassSize = db.ClassLayout.get_value<std::uint32_t>(i, 1);
            break;
        }
    } catch (const std::exception&) {
        // Malformed image: report no layout.
    }
    return result;
}

// The PE-section reads (PeImage passthroughs through the body reader).
// See the header for the full contract.
int MetadataFile::GetContainingSectionIndex(std::uint32_t rva) const {
    if (!IsValid() || !impl_->bodyReader) return -1;
    return impl_->bodyReader->GetContainingSectionIndex(rva);
}

std::string MetadataFile::GetSectionName(int sectionIndex) const {
    if (!IsValid() || !impl_->bodyReader) return {};
    return impl_->bodyReader->GetSectionName(sectionIndex);
}

// A HasFieldRVA field's initial value -- the C# SRMExtensions GetInitialValue
// (the null-typeSystem shape). See the header for the full contract.
std::vector<std::uint8_t> MetadataFile::GetFieldInitialValue(
        std::uint32_t fieldToken) const {
    // The C# `if (!field.HasFlag(FieldAttributes.HasFieldRVA)) return default;`
    // and `if (rva == 0) return default;` -- fields without data read empty.
    constexpr std::uint32_t kHasFieldRVA = 0x0100;  // FieldAttributes bit 8
    if ((GetFieldAttributes(fieldToken) & kHasFieldRVA) == 0) return {};
    std::uint32_t rva = GetFieldRVA(fieldToken);
    if (rva == 0) return {};

    // The C# `field.DecodeSignature(new FieldValueSizeDecoder(typeSystem:
    // null), default)` -- the field signature's one type decoded through the
    // size provider at the nil generic context (VAR/MVAR read 0). A
    // malformed blob propagates the walker's std::logic_error (the C#
    // BadImageFormatException out of DecodeFieldSignature).
    auto blob = GetSignatureBlob(fieldToken);
    if (!blob || blob->empty() || ((*blob)[0] & 0x0F) != 0x06)
        throw std::logic_error("field signature");
    FieldValueSizeDecoder sizeProvider(*this);
    SignatureTypeProviderDecoder<FieldValueSizeDecoder> decoder(
        sizeProvider, *this);
    int size = decoder.DecodeType(blob->data() + 1, blob->size() - 1,
        MetadataGenericContext::Nil());

    auto sectionData = impl_->bodyReader
        ? impl_->bodyReader->GetSectionData(rva)
        : PeImage::SectionDataView{};
    // The exact C# BadImageFormatException messages (the DisassembleField
    // catch renders them into the `// .data ...` comment line).
    if (sectionData.length == 0 && size != 0) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%x", rva);
        throw std::runtime_error(std::string(
            "Field data (rva=0x") + buf
            + ") could not be found in any section!");
    }
    if (size < 0 || static_cast<std::uint64_t>(size) > sectionData.length) {
        throw std::runtime_error(
            "Invalid size " + std::to_string(size) + " for field data!");
    }
    if (size == 0) return {};
    return std::vector<std::uint8_t>(sectionData.base,
        sectionData.base + size);
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

// A MethodDef row's ImplAttributes column (II.23.1.12). See the header for
// the full contract.
std::uint32_t MetadataFile::GetMethodImplAttributes(std::uint32_t methodToken) const {
    if (!IsValid()) return 0;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return 0;
    try {
        // The ImplFlags column is 2 bytes (II.23.1.12), widened to uint32.
        return impl_->db->MethodDef[row - 1].ImplFlags().value;
    } catch (const std::exception&) {
        return 0;
    }
}

// The ImplMap row whose MemberForwarded is the MethodDef -- the C#
// MethodDefinition.GetImport() (the SRM MethodImport: Module, Name,
// Attributes). See the header for the full contract.
std::optional<MetadataFile::MethodImportInfo> MetadataFile::GetMethodImport(
    std::uint32_t methodToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0)
        return std::nullopt;
    try {
        std::uint32_t count = static_cast<std::uint32_t>(impl_->db->ImplMap.size());
        // The MemberForwarded coded index (II.24.2.4): 1 tag bit, tag 1 =
        // MethodDef (the port's FieldMarshal scan convention for the sibling
        // HasFieldMarshal column).
        std::uint32_t want = (row << 1) | 1u;
        for (std::uint32_t i = 0; i < count; i++) {
            if (impl_->db->ImplMap.get_value<std::uint32_t>(i, 1) != want)
                continue;
            MethodImportInfo info;
            info.Attributes =
                impl_->db->ImplMap.get_value<std::uint32_t>(i, 0);
            std::uint32_t nameOffset =
                impl_->db->ImplMap.get_value<std::uint32_t>(i, 2);
            if (nameOffset != 0) {
                info.Name = std::string{impl_->db->get_string(nameOffset)};
            }
            std::uint32_t scope =
                impl_->db->ImplMap.get_value<std::uint32_t>(i, 3);
            if (scope != 0) {
                info.ModuleRefToken = (0x1Au << 24) | scope;
            }
            return info;
        }
        return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The MethodImpl rows whose MethodBody is the MethodDef -- the C#
// handle.GetMethodImplementations(metadata) ILSpy extension (the declaring
// type's Class-column range filtered by MethodBody; the port scans the whole
// table and filters on both the MethodBody column and the Class column --
// well-formed metadata has Class == the method's declaring type). See the
// header for the full contract.
std::vector<MetadataFile::MethodImplementationInfo>
MetadataFile::GetMethodImplementations(std::uint32_t methodToken) const {
    std::vector<MethodImplementationInfo> result;
    if (!IsValid()) return result;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0)
        return result;
    try {
        std::uint32_t declaringType = GetMethodDeclaringTypeToken(methodToken)
            & 0x00FFFFFFu;
        std::uint32_t count = static_cast<std::uint32_t>(impl_->db->MethodImpl.size());
        for (std::uint32_t i = 0; i < count; i++) {
            // MethodDefOrRef coded index (1 tag bit): the MethodBody column,
            // tag 0 = MethodDef; the Class column is a plain TypeDef row
            // index (the token's low 24 bits -- no coding).
            std::uint32_t bodyRaw =
                impl_->db->MethodImpl.get_value<std::uint32_t>(i, 1);
            if ((bodyRaw >> 1) != row || (bodyRaw & 1u) != 0)
                continue;
            if (impl_->db->MethodImpl.get_value<std::uint32_t>(i, 0)
                != declaringType)
                continue;
            MethodImplementationInfo info;
            info.Token = (0x19u << 24) | ((i + 1) & 0x00FFFFFFu);
            std::uint32_t declRaw =
                impl_->db->MethodImpl.get_value<std::uint32_t>(i, 2);
            if (declRaw != 0) {
                std::uint32_t declRid = declRaw >> 1;
                std::uint32_t declTable = (declRaw & 1u) ? 0x0Au : 0x06u;
                if (declRid != 0)
                    info.MethodDeclarationToken =
                        (declTable << 24) | declRid;
            }
            result.push_back(std::move(info));
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed table walk degrades to the partial result.
    }
    return result;
}

// The DeclSecurity rows whose Parent is the token -- the C#
// TypeDefinition/MethodDefinition.GetDeclarativeSecurityAttributes()
// collection. See the header for the full contract.
std::vector<MetadataFile::DeclarativeSecurityInfo>
MetadataFile::GetDeclarativeSecurityAttributes(std::uint32_t parentToken) const {
    std::vector<DeclarativeSecurityInfo> result;
    if (!IsValid()) return result;
    std::uint32_t row = parentToken & 0x00FFFFFFu;
    if (row == 0) return result;
    // The HasDeclSecurity coded index (II.24.2.4): 2 tag bits over TypeDef,
    // MethodDef, Assembly (the winmd composite includes the assembly-level
    // rows; the disassembler's member walks only reach the first two).
    std::uint32_t tag;
    switch (parentToken >> 24) {
        case 0x02: tag = 0; break;  // TypeDef
        case 0x06: tag = 1; break;  // MethodDef
        case 0x20: tag = 2; break;  // Assembly
        default: return result;
    }
    std::uint32_t want = (row << 2) | tag;
    try {
        std::uint32_t count = static_cast<std::uint32_t>(
            impl_->db->DeclSecurity.size());
        for (std::uint32_t i = 0; i < count; i++) {
            if (impl_->db->DeclSecurity.get_value<std::uint32_t>(i, 1) != want)
                continue;
            DeclarativeSecurityInfo info;
            info.Token = (0x0Eu << 24) | ((i + 1) & 0x00FFFFFFu);
            info.Action =
                impl_->db->DeclSecurity.get_value<std::uint32_t>(i, 0) & 0xFFFFu;
            std::uint32_t blobOffset =
                impl_->db->DeclSecurity.get_value<std::uint32_t>(i, 2);
            if (blobOffset != 0) {
                auto blob = impl_->db->get_blob(blobOffset);
                info.PermissionSet =
                    std::vector<std::uint8_t>(blob.begin(), blob.end());
            }
            result.push_back(std::move(info));
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed table walk degrades to the partial result.
    }
    return result;
}

// The MethodSemantics rows (table 0x18) whose Association is the
// property/event -- the C# PropertyDefinition.GetAccessors() walk. The
// Association column is a HasSemantics coded index (1 tag bit: 0=Event,
// 1=Property); the MethodSemantics column (the ECMA II.23.1
// MethodSemanticsAttributes bits: Setter 0x1, Getter 0x2, Other 0x4,
// AddOn 0x8, RemoveOn 0x10, Fire 0x20 -- see ReflectionAttributes.hpp) is
// an EXACT value match in the SRM switch (a row carrying combined flags
// matches no arm and is ignored); the LAST matching row wins the
// getter/setter slots (the C# switch assignment, no first-wins guard);
// the Others keep the row order. See the header for the full contract.
MetadataFile::PropertyAccessorsInfo MetadataFile::GetPropertyAccessors(
    std::uint32_t propertyToken) const
{
    PropertyAccessorsInfo result;
    if (!IsValid()) return result;
    std::uint32_t table = propertyToken >> 24;
    std::uint32_t row = propertyToken & 0x00FFFFFFu;
    if (table != 0x17 || row == 0) return result;
    try {
        std::uint32_t count =
            static_cast<std::uint32_t>(impl_->db->MethodSemantics.size());
        for (std::uint32_t i = 0; i < count; i++) {
            std::uint32_t association =
                impl_->db->MethodSemantics.get_value<std::uint32_t>(i, 2);
            if ((association & 0x1u) != 1u || (association >> 1) != row)
                continue;
            std::uint32_t methodRow =
                impl_->db->MethodSemantics.get_value<std::uint32_t>(i, 1);
            if (methodRow == 0) continue;
            std::uint32_t semantics =
                impl_->db->MethodSemantics.get_value<std::uint32_t>(i, 0);
            std::uint32_t methodToken =
                (0x06u << 24) | (methodRow & 0x00FFFFFFu);
            switch (semantics) {
                case 0x2:  // MethodSemanticsAttributes::Getter
                    result.GetterToken = methodToken;
                    break;
                case 0x1:  // MethodSemanticsAttributes::Setter
                    result.SetterToken = methodToken;
                    break;
                case 0x4:  // MethodSemanticsAttributes::Other
                    result.OtherTokens.push_back(methodToken);
                    break;
                default:
                    break;
            }
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed table walk degrades to the partial result.
    }
    return result;
}

// The event twin of GetPropertyAccessors -- the C#
// EventDefinition.GetAccessors() (the AddOn/RemoveOn/Fire/Other arms of the
// same exact-value switch). See the header for the full contract.
MetadataFile::EventAccessorsInfo MetadataFile::GetEventAccessors(
    std::uint32_t eventToken) const
{
    EventAccessorsInfo result;
    if (!IsValid()) return result;
    std::uint32_t table = eventToken >> 24;
    std::uint32_t row = eventToken & 0x00FFFFFFu;
    if (table != 0x14 || row == 0) return result;
    try {
        std::uint32_t count =
            static_cast<std::uint32_t>(impl_->db->MethodSemantics.size());
        for (std::uint32_t i = 0; i < count; i++) {
            std::uint32_t association =
                impl_->db->MethodSemantics.get_value<std::uint32_t>(i, 2);
            if ((association & 0x1u) != 0u || (association >> 1) != row)
                continue;
            std::uint32_t methodRow =
                impl_->db->MethodSemantics.get_value<std::uint32_t>(i, 1);
            if (methodRow == 0) continue;
            std::uint32_t semantics =
                impl_->db->MethodSemantics.get_value<std::uint32_t>(i, 0);
            std::uint32_t methodToken =
                (0x06u << 24) | (methodRow & 0x00FFFFFFu);
            switch (semantics) {
                case 0x8:  // MethodSemanticsAttributes::AddOn
                    result.AdderToken = methodToken;
                    break;
                case 0x10:  // MethodSemanticsAttributes::RemoveOn
                    result.RemoverToken = methodToken;
                    break;
                case 0x20:  // MethodSemanticsAttributes::Fire
                    result.RaiserToken = methodToken;
                    break;
                case 0x4:  // MethodSemanticsAttributes::Other
                    result.OtherTokens.push_back(methodToken);
                    break;
                default:
                    break;
            }
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed table walk degrades to the partial result.
    }
    return result;
}

// An Event row's EventType column (the TypeDefOrRef coded index). See the
// header for the full contract.
std::uint32_t MetadataFile::GetEventTypeToken(
    std::uint32_t eventToken) const
{
    if (!IsValid()) return 0;
    std::uint32_t table = eventToken >> 24;
    std::uint32_t row = eventToken & 0x00FFFFFFu;
    if (table != 0x14 || row == 0 || row > impl_->db->Event.size()) return 0;
    try {
        std::uint32_t v = impl_->db->Event.get_value<std::uint32_t>(row - 1, 2);
        if (v == 0) return 0;
        std::uint32_t rid = v >> 2;
        if (rid == 0) return 0;
        switch (v & 0x3u) {
            case 0:  // TypeDef
                return (0x02u << 24) | rid;
            case 1:  // TypeRef
                return (0x01u << 24) | rid;
            case 2:  // TypeSpec
                return (0x1Bu << 24) | rid;
        }
        return 0;
    } catch (const std::exception&) {
        return 0;
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

// The CustomAttribute row tokens of an entity (the HasCustomAttribute scan) --
// see the header for the full contract. The raw Parent column is
// ((row 1-based) << 5) | tag over the 22 HasCustomAttribute parent kinds.
std::vector<std::uint32_t> MetadataFile::GetCustomAttributeTokens(
    std::uint32_t entityToken) const {
    std::vector<std::uint32_t> result;
    if (!IsValid()) return result;
    std::uint32_t row = entityToken & 0x00FFFFFFu;
    if (row == 0) return result;
    // The HasCustomAttribute tag of the parent's table (II.24.2.6): the
    // composite index order MethodDef, Field, TypeRef, TypeDef, Param,
    // InterfaceImpl, MemberRef, Module, DeclSecurity, Property, Event,
    // StandAloneSig, ModuleRef, TypeSpec, Assembly, AssemblyRef, File,
    // ExportedType, ManifestResource, GenericParam, GenericParamConstraint,
    // MethodSpec.
    std::uint32_t tag;
    switch (entityToken >> 24) {
        case 0x06: tag = 0; break;   // MethodDef
        case 0x04: tag = 1; break;   // Field
        case 0x01: tag = 2; break;   // TypeRef
        case 0x02: tag = 3; break;   // TypeDef
        case 0x08: tag = 4; break;   // Param
        case 0x09: tag = 5; break;   // InterfaceImpl
        case 0x0A: tag = 6; break;   // MemberRef
        case 0x00: tag = 7; break;   // Module
        case 0x0D: tag = 8; break;  // DeclSecurity (Permission)
        case 0x17: tag = 9; break;  // Property
        case 0x14: tag = 10; break;  // Event
        case 0x11: tag = 11; break;  // StandAloneSig
        case 0x1A: tag = 12; break;  // ModuleRef
        case 0x1B: tag = 13; break;  // TypeSpec
        case 0x20: tag = 14; break;  // Assembly
        case 0x23: tag = 15; break;  // AssemblyRef
        case 0x26: tag = 16; break;  // File
        case 0x27: tag = 17; break;  // ExportedType
        case 0x28: tag = 18; break;  // ManifestResource
        case 0x2A: tag = 19; break;  // GenericParam
        case 0x2C: tag = 20; break;  // GenericParamConstraint
        case 0x2B: tag = 21; break;  // MethodSpec
        default: return result;
    }
    std::uint32_t want = (row << 5) | tag;
    try {
        std::uint32_t count = static_cast<std::uint32_t>(
            impl_->db->CustomAttribute.size());
        for (std::uint32_t i = 0; i < count; i++) {
            if (impl_->db->CustomAttribute.get_value<std::uint32_t>(i, 0)
                != want)
                continue;
            result.push_back((0x0Cu << 24) | ((i + 1) & 0x00FFFFFFu));
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed table walk degrades to the partial result.
    }
    return result;
}

// One raw CustomAttribute row (the C# metadata.GetCustomAttribute(handle)).
// The Type column is a CustomAttributeType coded index with 3 tag bits and
// the unusual tag values 2 (MethodDef) and 3 (MemberRef) -- 0 and 1 are
// reserved so the value cannot be confused with a TypeDefOrRef/MethodDefOrRef
// (II.24.2.6). An invalid tag reads as a nil constructor token (the
// never-throw read contract; the C# throws BadImageFormatException on the
// corrupt row instead -- a documented divergence confined to bad metadata).
std::optional<CustomAttributeRowInfo>
MetadataFile::GetCustomAttribute(std::uint32_t attributeToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = attributeToken >> 24;
    std::uint32_t row = attributeToken & 0x00FFFFFFu;
    if (table != 0x0C || row == 0
        || row > impl_->db->CustomAttribute.size())
        return std::nullopt;
    try {
        CustomAttributeRowInfo info;
        info.Token = attributeToken;
        std::uint32_t ctorRaw =
            impl_->db->CustomAttribute.get_value<std::uint32_t>(row - 1, 1);
        std::uint32_t ctorRid = ctorRaw >> 3;
        switch (ctorRaw & 0x7u) {
            case 2:  // MethodDef
                if (ctorRid != 0) info.ConstructorToken = (0x06u << 24) | ctorRid;
                break;
            case 3:  // MemberRef
                if (ctorRid != 0) info.ConstructorToken = (0x0Au << 24) | ctorRid;
                break;
            default:
                break;
        }
        std::uint32_t blobOffset =
            impl_->db->CustomAttribute.get_value<std::uint32_t>(row - 1, 2);
        if (blobOffset != 0) {
            auto blob = impl_->db->get_blob(blobOffset);
            info.ValueBlob = std::vector<std::uint8_t>(blob.begin(), blob.end());
        }
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
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

std::vector<ParameterInfo> MetadataFile::GetParameters(std::uint32_t methodToken) const {
    std::vector<ParameterInfo> result;
    if (!IsValid()) return result;
    std::uint32_t table = methodToken >> 24;
    std::uint32_t row = methodToken & 0x00FFFFFFu;
    if (table != 0x06 || row == 0 || row > impl_->db->MethodDef.size()) return result;
    try {
        auto& db = *impl_->db;
        auto m = db.MethodDef[row - 1];
        auto range = m.ParamList();
        for (auto it = range.first; it != range.second; ++it) {
            ParameterInfo info;
            unsigned paramRow = (unsigned)it.index() + 1;  // 1-based
            info.Token = 0x08000000u | paramRow;
            info.SequenceNumber = db.Param.get_value<std::uint16_t>(paramRow - 1, 1);
            // The 2-byte Flags column widened to uint32 (the BCL enum read).
            info.Attributes = db.Param.get_value<std::uint16_t>(paramRow - 1, 0);
            info.Name = std::string(db.Param[paramRow - 1].Name());
            // The marshalling descriptor: the FieldMarshal row whose
            // HasFieldMarshal coded index (Field tag 0, Param tag 1 --
            // 1 tag bit) points at this row. The C# p.GetMarshallingDescriptor()
            // IsNil test ports to the row's absence.
            std::uint32_t want = (paramRow << 1) | 1;
            for (unsigned j = 0; j < db.FieldMarshal.size(); ++j) {
                if (db.FieldMarshal.get_value<std::uint32_t>(j, 0) == want) {
                    auto blob = db.get_blob(
                        db.FieldMarshal.get_value<std::uint32_t>(j, 1));
                    info.MarshallingDescriptor = std::vector<std::uint8_t>(
                        blob.begin(), blob.end());
                    break;
                }
            }
            result.push_back(std::move(info));
        }
    } catch (const std::exception&) {
        // Best-effort: a malformed row leaves the result short.
    }
    return result;
}

std::optional<ConstantInfo> MetadataFile::GetConstant(std::uint32_t parentToken) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = parentToken >> 24;
    std::uint32_t row = parentToken & 0x00FFFFFFu;
    std::uint32_t tag;
    switch (table) {
        case 0x04: tag = 0; break;  // Field
        case 0x08: tag = 1; break;  // Param
        case 0x17: tag = 2; break;  // Property
        default: return std::nullopt;
    }
    if (row == 0) return std::nullopt;
    try {
        auto& db = *impl_->db;
        // The HasConstant coded index: 2 tag bits, 1-based row.
        std::uint32_t want = (row << 2) | tag;
        for (std::uint32_t i = 0; i < db.Constant.size(); ++i) {
            if (db.Constant.get_value<std::uint32_t>(i, 1) != want) continue;
            ConstantInfo info;
            info.Token = 0x0B000000u | (i + 1);
            // The Type column is physically 2 bytes; SRM's TypeCode reads
            // the byte at offset 0 (the low byte).
            info.TypeCode = static_cast<std::uint8_t>(
                db.Constant.get_value<std::uint16_t>(i, 0) & 0xFFu);
            auto blob = db.get_blob(db.Constant.get_value<std::uint32_t>(i, 2));
            info.Value.assign(blob.begin(), blob.end());
            return info;
        }
    } catch (const std::exception&) {
        // Malformed image: report no constant.
    }
    return std::nullopt;
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

// The Assembly table's single row (row 1 -- ECMA allows exactly one
// assembly-manifest row). See the header for the full contract.
std::optional<MetadataFile::AssemblyDefinitionInfo>
MetadataFile::GetAssemblyDefinition() const {
    if (!IsValid() || impl_->db->Assembly.size() == 0)
        return std::nullopt;
    try {
        AssemblyDefinitionInfo info;
        info.Token = (0x20u << 24) | 1u;
        info.Name = std::string{impl_->db->Assembly[0].Name()};
        info.Flags = impl_->db->Assembly[0].Flags().value;
        info.HashAlgorithm =
            static_cast<std::uint32_t>(impl_->db->Assembly[0].HashAlgId());
        auto version = impl_->db->Assembly[0].Version();
        info.MajorVersion = version.MajorVersion;
        info.MinorVersion = version.MinorVersion;
        info.BuildNumber = version.BuildNumber;
        info.RevisionNumber = version.RevisionNumber;
        // A raw column value of 0 is the nil blob handle (the C#
        // `asm.PublicKey.IsNil`); anything else materializes the bytes.
        std::uint32_t publicKeyOffset =
            impl_->db->Assembly.get_value<std::uint32_t>(0, 3);
        if (publicKeyOffset != 0) {
            auto blob = impl_->db->get_blob(publicKeyOffset);
            info.PublicKey =
                std::vector<std::uint8_t>(blob.begin(), blob.end());
        }
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The AssemblyRef rows in table order. See the header for the full contract.
std::vector<MetadataFile::AssemblyReferenceInfo>
MetadataFile::GetAssemblyReferences() const {
    std::vector<AssemblyReferenceInfo> result;
    if (!IsValid()) return result;
    try {
        for (std::uint32_t row = 1;
             row <= impl_->db->AssemblyRef.size(); row++) {
            AssemblyReferenceInfo info;
            info.Token = (0x23u << 24) | row;
            info.Name = std::string{impl_->db->AssemblyRef[row - 1].Name()};
            info.Flags = impl_->db->AssemblyRef[row - 1].Flags().value;
            auto version = impl_->db->AssemblyRef[row - 1].Version();
            info.MajorVersion = version.MajorVersion;
            info.MinorVersion = version.MinorVersion;
            info.BuildNumber = version.BuildNumber;
            info.RevisionNumber = version.RevisionNumber;
            std::uint32_t publicKeyOffset =
                impl_->db->AssemblyRef.get_value<std::uint32_t>(row - 1, 2);
            if (publicKeyOffset != 0) {
                auto blob = impl_->db->get_blob(publicKeyOffset);
                info.PublicKeyOrToken =
                    std::vector<std::uint8_t>(blob.begin(), blob.end());
            }
            result.push_back(std::move(info));
        }
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

// The ModuleRef rows in table order. See the header for the full contract.
std::vector<MetadataFile::ModuleReferenceInfo>
MetadataFile::GetModuleReferences() const {
    std::vector<ModuleReferenceInfo> result;
    if (!IsValid()) return result;
    try {
        for (std::uint32_t row = 1;
             row <= impl_->db->ModuleRef.size(); row++) {
            ModuleReferenceInfo info;
            info.Token = (0x1Au << 24) | row;
            // The ModuleRef row exposes no Name() accessor in the winmd
            // reader -- the raw string column read (the GetModuleReferenceName
            // convention).
            info.Name = std::string{impl_->db->get_string(
                impl_->db->ModuleRef.get_value<std::uint32_t>(row - 1, 0))};
            result.push_back(std::move(info));
        }
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

// The ExportedType rows (table 0x27) in table order -- the C#
// `metadata.ExportedTypes` collection. See the header for the full
// contract. Column layout (II.22.14): Flags(4), TypeDefId(4), TypeName,
// TypeNamespace, Implementation (the coded index over File/AssemblyRef/
// ExportedType; winmd's composite_index_size(File, AssemblyRef,
// ExportedType) fixes the tag order 0/1/2).
std::vector<MetadataFile::ExportedTypeInfo>
MetadataFile::GetExportedTypes() const {
    std::vector<ExportedTypeInfo> result;
    if (!IsValid()) return result;
    std::uint32_t count = static_cast<std::uint32_t>(
        impl_->db->ExportedType.size());
    for (std::uint32_t row = 1; row <= count; row++) {
        try {
            ExportedTypeInfo info;
            info.Token = (0x27u << 24) | row;
            info.Attributes =
                impl_->db->ExportedType.get_value<std::uint32_t>(row - 1, 0);
            info.TypeDefinitionId =
                impl_->db->ExportedType.get_value<std::uint32_t>(row - 1, 1);
            info.Name = std::string{impl_->db->get_string(
                impl_->db->ExportedType.get_value<std::uint32_t>(row - 1, 2))};
            std::uint32_t nsIndex =
                impl_->db->ExportedType.get_value<std::uint32_t>(row - 1, 3);
            info.NamespaceNil = nsIndex == 0;
            if (!info.NamespaceNil)
                info.Namespace = std::string{impl_->db->get_string(nsIndex)};
            // The Implementation coded index: 2 tag bits, the row shifted up.
            std::uint32_t raw =
                impl_->db->ExportedType.get_value<std::uint32_t>(row - 1, 4);
            std::uint32_t tag = raw & 0x3u;
            std::uint32_t target = raw >> 2;
            switch (tag) {
                case 0: info.ImplementationToken = (0x26u << 24) | target; break;
                case 1: info.ImplementationToken = (0x23u << 24) | target; break;
                case 2: info.ImplementationToken = (0x27u << 24) | target; break;
                default: info.ImplementationToken = 0; break;
            }
            result.push_back(std::move(info));
        } catch (const std::exception&) {
            // Malformed table walk: stop at the first unreadable row (the
            // rows before it stay readable).
            break;
        }
    }
    return result;
}

// One ExportedType row by its own token. See the header for the full
// contract.
std::optional<MetadataFile::ExportedTypeInfo>
MetadataFile::GetExportedType(std::uint32_t token) const {
    if (!IsValid()) return std::nullopt;
    if ((token >> 24) != 0x27u) return std::nullopt;
    std::uint32_t row = token & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->ExportedType.size())
        return std::nullopt;
    for (const auto& et : GetExportedTypes())
        if (et.Token == token) return et;
    return std::nullopt;
}

// A File-table (table 0x26) row's Name -- the exported-type block's
// `.file <name>` line. See the header for the full contract.
std::optional<std::string> MetadataFile::GetAssemblyFileName(
    std::uint32_t token) const {
    if (!IsValid()) return std::nullopt;
    if ((token >> 24) != 0x26u) return std::nullopt;
    std::uint32_t row = token & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->File.size()) return std::nullopt;
    try {
        return std::string{impl_->db->get_string(
            impl_->db->File.get_value<std::uint32_t>(row - 1, 1))};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// An AssemblyRef row's Name by token. See the header for the full contract.
// The Name reads through the winmd row accessor (winmd merges the version
// columns, so its raw column order is not ECMA's).
std::optional<std::string> MetadataFile::GetAssemblyReferenceName(
    std::uint32_t token) const {
    if (!IsValid()) return std::nullopt;
    if ((token >> 24) != 0x23u) return std::nullopt;
    std::uint32_t row = token & 0x00FFFFFFu;
    if (row == 0 || row > impl_->db->AssemblyRef.size()) return std::nullopt;
    try {
        return std::string{impl_->db->AssemblyRef[row - 1].Name()};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The Module table's single row 1 -- the C# `metadata.GetModuleDefinition()`.
// See the header for the full contract. Column layout (II.22.30):
// Generation(2), Name, Mvid (a #GUID heap index), EncId, EncBaseId.
std::optional<MetadataFile::ModuleDefinitionInfo>
MetadataFile::GetModuleDefinition() const {
    if (!IsValid() || impl_->db->Module.size() == 0)
        return std::nullopt;
    try {
        ModuleDefinitionInfo info;
        info.Name = std::string{impl_->db->Module[0].Name()};
        std::uint32_t mvidIndex =
            impl_->db->Module.get_value<std::uint32_t>(0, 2);
        // The #GUID heap read (MetadataReader.GetGuid): a nil index is
        // Guid.Empty (the all-zeros array the TryGetGuid nil arm returns).
        if (impl_->bodyReader) {
            auto guid = impl_->bodyReader->TryGetGuid(mvidIndex);
            if (guid) info.Mvid = *guid;
        }
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The top-level TypeDef tokens in row order -- the C# ILSpy
// MetadataExtensions.GetTopLevelTypeDefinitions(reader) extension. See the
// header for the full contract. A row is top-level when no NestedClass
// table row names it as the nested type (the SRM
// TypeDefinition.GetDeclaringType back-scan).
std::vector<std::uint32_t> MetadataFile::GetTopLevelTypeDefinitions() const {
    std::vector<std::uint32_t> result;
    if (!IsValid()) return result;
    try {
        // The set of nested TypeDef row numbers -- the NestedClass table's
        // first column (the nested type; the second names the enclosing
        // class), in row order (the same rows GetNestedTypes walks).
        std::vector<std::uint32_t> nestedRows;
        nestedRows.reserve(impl_->db->NestedClass.size());
        for (std::uint32_t i = 0;
             i < impl_->db->NestedClass.size(); i++) {
            nestedRows.push_back(
                impl_->db->NestedClass.get_value<std::uint32_t>(i, 0));
        }
        for (std::uint32_t row = 1;
             row <= impl_->db->TypeDef.size(); row++) {
            if (std::find(nestedRows.begin(), nestedRows.end(), row)
                == nestedRows.end()) {
                result.push_back((0x02u << 24) | row);
            }
        }
    } catch (const std::exception&) {
        result.clear();
    }
    return result;
}

// The C# `public TypeDefinitionHandle GetTypeDefinition(TopLevelTypeName
// typeName)` (MetadataFile.cs line 167): the top-level-type reverse lookup.
// See the header for the full contract.
std::uint32_t MetadataFile::GetTypeDefinition(
    const TypeSystem::TopLevelTypeName& typeName) const {
    if (!IsValid()) return 0;
    try {
        if (!impl_->typeLookupBuilt) {
            // The C# build walk: every non-nested TypeDef row keyed by its
            // (namespace, arity-split name, count) triple, the dictionary
            // indexer overwriting duplicates (the LAST row wins).
            std::vector<std::uint32_t> nestedRows;
            nestedRows.reserve(impl_->db->NestedClass.size());
            for (std::uint32_t i = 0;
                 i < impl_->db->NestedClass.size(); i++) {
                nestedRows.push_back(
                    impl_->db->NestedClass.get_value<std::uint32_t>(i, 0));
            }
            for (std::uint32_t row = 1;
                 row <= impl_->db->TypeDef.size(); row++) {
                if (std::find(nestedRows.begin(), nestedRows.end(), row)
                    != nestedRows.end())
                    continue;  // the C# `td.GetDeclaringType().IsNil` skip
                auto t = impl_->db->TypeDef[row - 1];
                int typeParameterCount = 0;
                std::string name =
                    TypeSystem::SplitTypeParameterCountFromReflectionName(
                        std::string{t.TypeName()}, typeParameterCount);
                impl_->typeLookup[TypeSystem::TopLevelTypeName(
                                       std::string{t.TypeNamespace()}, name,
                                       typeParameterCount)] =
                    (0x02u << 24) | row;
            }
            impl_->typeLookupBuilt = true;
        }
        auto it = impl_->typeLookup.find(typeName);
        return it == impl_->typeLookup.end() ? 0 : it->second;
    } catch (const std::exception&) {
        // Corrupt columns inside an otherwise-valid file degrade to the
        // miss arm (the never-throw surface convention).
        return 0;
    }
}

// The C# `public ExportedTypeHandle GetTypeForwarder(FullTypeName typeName)`
// (MetadataFile.cs line 203): the type-forwarder reverse lookup. See the
// header for the full contract.
std::uint32_t MetadataFile::GetTypeForwarder(
    const TypeSystem::FullTypeName& typeName) const {
    if (!IsValid()) return 0;
    try {
        if (!impl_->typeForwarderLookupBuilt) {
            // The C# build walk: every ExportedType row keyed by its full
            // name through the ExportedType reader (nested rows key on
            // their declaring-chain names), the LAST duplicate winning.
            for (const auto& row : GetExportedTypes()) {
                impl_->typeForwarderLookup[
                    GetFullTypeNameFromExportedType(*this, row.Token)] =
                    row.Token;
            }
            impl_->typeForwarderLookupBuilt = true;
        }
        auto it = impl_->typeForwarderLookup.find(typeName);
        return it == impl_->typeForwarderLookup.end() ? 0 : it->second;
    } catch (const std::exception&) {
        // Corrupt rows degrade to the miss arm (the never-throw surface
        // convention; the C# BadImageFormatException out of the reader
        // surfaces differently, but the CLI never reaches it).
        return 0;
    }
}

// The namespace-definition tree (the MetadataReader's GetNamespaceDefinition
// Root / GetNamespaceDefinition / GetString(NamespaceDefinitionHandle)
// surface). See the header and NamespaceDefinition.hpp for the contracts.
const NamespaceDefinition& MetadataFile::GetNamespaceDefinitionRoot() const {
    return impl_->Namespaces(this).GetRootNamespace();
}

const NamespaceDefinition& MetadataFile::GetNamespaceDefinition(
    NamespaceDefinitionHandle handle) const {
    return impl_->Namespaces(this).GetNamespaceData(handle);
}

std::string MetadataFile::GetNamespaceString(
    NamespaceDefinitionHandle handle) const {
    return impl_->Namespaces(this).GetFullName(handle);
}

std::vector<NamespaceDefinitionHandle>
MetadataFile::GetNamespaceDefinitionHandlesInTableOrder() const {
    return impl_->Namespaces(this).GetHandlesInTableOrder();
}

// The PE-header values the module-header lines render. See the header for
// the full contract.
std::optional<MetadataFile::PeHeaderInfo> MetadataFile::GetPeHeaderInfo()
    const {
    if (!IsValid() || !impl_->bodyReader || !impl_->bodyReader->HasImage())
        return std::nullopt;
    PeHeaderInfo info;
    info.ImageBase = impl_->bodyReader->ImageBase();
    info.FileAlignment = impl_->bodyReader->FileAlignment();
    info.SizeOfStackReserve = impl_->bodyReader->SizeOfStackReserve();
    info.Subsystem = impl_->bodyReader->Subsystem();
    info.CorFlags = impl_->bodyReader->CorFlags();
    return info;
}

// The ManifestResource rows (table 0x28) in table order -- the C#
// `MetadataFile.Resources` collection. See the header for the full
// contract. Column layout (II.22.24): Offset(4), Flags(4), Name,
// Implementation (the coded index over File/AssemblyRef/ExportedType;
// winmd's composite_index_size fixes the tag order 0/1/2).
std::vector<MetadataFile::ManifestResourceInfo>
MetadataFile::GetManifestResources() const {
    std::vector<ManifestResourceInfo> result;
    if (!IsValid()) return result;
    std::uint32_t count = static_cast<std::uint32_t>(
        impl_->db->ManifestResource.size());
    for (std::uint32_t row = 1; row <= count; row++) {
        try {
            ManifestResourceInfo info;
            info.Token = (0x28u << 24) | row;
            info.Offset = impl_->db->ManifestResource.get_value<std::uint32_t>(
                row - 1, 0);
            info.Attributes =
                impl_->db->ManifestResource.get_value<std::uint32_t>(
                    row - 1, 1);
            info.Name = std::string{impl_->db->get_string(
                impl_->db->ManifestResource.get_value<std::uint32_t>(
                    row - 1, 2))};
            // The Implementation coded index: 2 tag bits, the row shifted
            // up; 0=File, 1=AssemblyRef, 2=ExportedType (the dumper's
            // DecodeImplementation mapping).
            std::uint32_t raw = impl_->db->ManifestResource.get_value<
                std::uint32_t>(row - 1, 3);
            static constexpr std::uint32_t kTables[3] = {0x26, 0x23, 0x27};
            if (raw != 0) {
                std::uint32_t tag = raw & 0x3u;
                std::uint32_t rid = raw >> 2;
                if (rid != 0 && tag < 3)
                    info.ImplementationToken =
                        (kTables[tag] << 24) | rid;
            }
            // The C# MetadataResource.GetResourceType: a nil
            // implementation is Embedded, an AssemblyReference target is
            // AssemblyLinked, anything else Linked.
            if (info.ImplementationToken == 0) {
                info.Kind = ManifestResourceKind::Embedded;
            } else if ((info.ImplementationToken >> 24) == 0x23) {
                info.Kind = ManifestResourceKind::AssemblyLinked;
            } else {
                info.Kind = ManifestResourceKind::Linked;
            }
            result.push_back(std::move(info));
        } catch (const std::exception&) {
            // Malformed table walk: stop at the first unreadable row (the
            // rows before it stay readable).
            break;
        }
    }
    return result;
}

// An embedded ManifestResource's blob -- the C#
// MetadataResource.TryOpenStream/TryGetLength pair. See the header for
// the full contract.
std::optional<std::vector<std::uint8_t>>
MetadataFile::TryGetManifestResourceData(std::uint32_t token) const {
    if (!IsValid()) return std::nullopt;
    std::uint32_t table = token >> 24;
    std::uint32_t row = token & 0x00FFFFFFu;
    if (table != 0x28 || row == 0
        || row > impl_->db->ManifestResource.size())
        return std::nullopt;
    try {
        std::uint32_t implementation = impl_->db->ManifestResource.get_value<
            std::uint32_t>(row - 1, 3);
        std::uint32_t kind;
        if (implementation == 0) {
            kind = 0;  // nil: embedded
        } else if ((implementation & 0x3u) == 1) {
            kind = 1;  // AssemblyRef: assembly-linked
        } else {
            kind = 2;  // File/ExportedType: linked
        }
        // The C# `if (ResourceType != ResourceType.Embedded) return false`:
        // only embedded resources carry a blob in this file.
        if (kind != 0) return std::nullopt;
        // The C# `if (Module.CorHeader == null) return false` and the
        // `resources.RelativeVirtualAddress <= 0` guard: no cor20
        // Resources directory means no embedded resource can be read.
        if (!impl_->bodyReader || !impl_->bodyReader->HasImage())
            return std::nullopt;
        std::uint32_t resourcesRva =
            impl_->bodyReader->ResourcesDirectoryRva();
        if (resourcesRva == 0) return std::nullopt;
        PeImage::SectionDataView sectionData =
            impl_->bodyReader->GetSectionData(resourcesRva);
        // Validate section length: we need at least 4 bytes to extract
        // the actual length of the resource blob.
        if (sectionData.length < 4) return std::nullopt;
        std::uint32_t offset = impl_->db->ManifestResource.get_value<
            std::uint32_t>(row - 1, 0);
        // Validate resource offset (the C# `offset < 0 || offset >
        // sectionData.Length - 4`; the column is unsigned, so the negative
        // arm is the C#'s int read of the raw bits).
        if (offset > sectionData.length - 4) return std::nullopt;
        const std::uint8_t* ptr = sectionData.base + offset;
        // Get actual length of resource blob.
        std::uint64_t length = static_cast<std::uint64_t>(ptr[0])
            | (static_cast<std::uint64_t>(ptr[1]) << 8)
            | (static_cast<std::uint64_t>(ptr[2]) << 16)
            | (static_cast<std::uint64_t>(ptr[3]) << 24);
        if (length > sectionData.length) return std::nullopt;
        // The stream the C# returns starts after the length prefix; the
        // copy clamps at the section data's end (the nominal overrun the
        // upstream length check permits never leaves the image).
        std::size_t available = sectionData.length
            - (static_cast<std::size_t>(offset) + 4);
        std::size_t take = static_cast<std::size_t>(
            std::min<std::uint64_t>(length, available));
        return std::vector<std::uint8_t>(ptr + 4, ptr + 4 + take);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The PE debug-directory entries. See the header for the full contract.
std::vector<MetadataFile::DebugDirectoryEntryInfo>
MetadataFile::GetDebugDirectoryEntries() const {
    if (!IsValid() || !impl_->bodyReader || !impl_->bodyReader->HasImage())
        return {};
    std::vector<DebugDirectoryEntryInfo> result;
    for (const auto& e : impl_->bodyReader->ReadDebugDirectory()) {
        DebugDirectoryEntryInfo info;
        info.Stamp = e.Stamp;
        info.MajorVersion = e.MajorVersion;
        info.MinorVersion = e.MinorVersion;
        info.Type = static_cast<Disassembler::DebugDirectoryEntryType>(e.Type);
        info.DataSize = e.DataSize;
        info.DataRelativeVirtualAddress = e.DataRelativeVirtualAddress;
        info.DataPointer = e.DataPointer;
        result.push_back(info);
    }
    return result;
}

// The CV_INFO_PDB70 blob a CodeView entry points at. See the header for
// the full contract.
std::optional<MetadataFile::CodeViewDebugDirectoryDataInfo>
MetadataFile::GetCodeViewDebugDirectoryData(
    const DebugDirectoryEntryInfo& entry) const {
    if (!IsValid() || !impl_->bodyReader || !impl_->bodyReader->HasImage())
        return std::nullopt;
    PeImage::DebugDirectoryEntry internal;
    internal.Stamp = entry.Stamp;
    internal.MajorVersion = entry.MajorVersion;
    internal.MinorVersion = entry.MinorVersion;
    internal.Type = static_cast<std::int32_t>(entry.Type);
    internal.DataSize = entry.DataSize;
    internal.DataRelativeVirtualAddress = entry.DataRelativeVirtualAddress;
    internal.DataPointer = entry.DataPointer;
    auto data = impl_->bodyReader->ReadCodeViewDebugDirectoryData(internal);
    CodeViewDebugDirectoryDataInfo info;
    info.Guid = data.Guid;
    info.Age = data.Age;
    info.Path = std::move(data.Path);
    return info;
}

// The internal PeImage entry a DebugDirectoryEntryInfo maps onto (the
// field-by-field copy the two debug-directory passthroughs perform).
static PeImage::DebugDirectoryEntry ToInternalEntry(
    const MetadataFile::DebugDirectoryEntryInfo& entry) {
    PeImage::DebugDirectoryEntry internal;
    internal.Stamp = entry.Stamp;
    internal.MajorVersion = entry.MajorVersion;
    internal.MinorVersion = entry.MinorVersion;
    internal.Type = static_cast<std::int32_t>(entry.Type);
    internal.DataSize = entry.DataSize;
    internal.DataRelativeVirtualAddress = entry.DataRelativeVirtualAddress;
    internal.DataPointer = entry.DataPointer;
    return internal;
}

// The associated/embedded portable-PDB discovery. See the header for the
// full contract.
bool MetadataFile::TryOpenAssociatedPortablePdb(const std::string& peImagePath,
    const PdbStreamProvider& pdbFileStreamProvider,
    PortablePdb& pdbReaderProvider, std::string& pdbPath) const {
    if (!IsValid() || !impl_->bodyReader || !impl_->bodyReader->HasImage()) {
        pdbReaderProvider = PortablePdb(nullptr);
        pdbPath.clear();
        return false;
    }
    return impl_->bodyReader->TryOpenAssociatedPortablePdb(
        peImagePath, pdbFileStreamProvider, pdbReaderProvider, pdbPath);
}

// The MPDB blob an EmbeddedPortablePdb entry points at. See the header for
// the full contract.
PortablePdb MetadataFile::ReadEmbeddedPortablePdbDebugDirectoryData(
    const DebugDirectoryEntryInfo& entry) const {
    if (!IsValid() || !impl_->bodyReader || !impl_->bodyReader->HasImage())
        throw std::invalid_argument("entry is not an EmbeddedPortablePdb entry");
    return impl_->bodyReader->ReadEmbeddedPortablePdbDebugDirectoryData(
        ToInternalEntry(entry));
}

// --- The raw Cor-table row surface (the --dump-table path) ---

namespace {
// The winmd table a CorTableIndex id addresses (nullptr for the *Ptr
// indirection tables, which winmd cannot model -- a file carrying rows
// there fails to open -- and for ids outside the modeled set).
const winmd::reader::table_base* CorTableLookup(
    const winmd::reader::database& db, CorTableIndex table) {
    switch (table) {
        case CorTableIndex::Module: return &db.Module;
        case CorTableIndex::TypeRef: return &db.TypeRef;
        case CorTableIndex::TypeDef: return &db.TypeDef;
        case CorTableIndex::Field: return &db.Field;
        case CorTableIndex::MethodDef: return &db.MethodDef;
        case CorTableIndex::Param: return &db.Param;
        case CorTableIndex::InterfaceImpl: return &db.InterfaceImpl;
        case CorTableIndex::MemberRef: return &db.MemberRef;
        case CorTableIndex::Constant: return &db.Constant;
        case CorTableIndex::CustomAttribute: return &db.CustomAttribute;
        case CorTableIndex::FieldMarshal: return &db.FieldMarshal;
        case CorTableIndex::DeclSecurity: return &db.DeclSecurity;
        case CorTableIndex::ClassLayout: return &db.ClassLayout;
        case CorTableIndex::FieldLayout: return &db.FieldLayout;
        case CorTableIndex::StandAloneSig: return &db.StandAloneSig;
        case CorTableIndex::EventMap: return &db.EventMap;
        case CorTableIndex::Event: return &db.Event;
        case CorTableIndex::PropertyMap: return &db.PropertyMap;
        case CorTableIndex::Property: return &db.Property;
        case CorTableIndex::MethodSemantics: return &db.MethodSemantics;
        case CorTableIndex::MethodImpl: return &db.MethodImpl;
        case CorTableIndex::ModuleRef: return &db.ModuleRef;
        case CorTableIndex::TypeSpec: return &db.TypeSpec;
        case CorTableIndex::ImplMap: return &db.ImplMap;
        case CorTableIndex::FieldRva: return &db.FieldRVA;
        case CorTableIndex::Assembly: return &db.Assembly;
        case CorTableIndex::AssemblyRef: return &db.AssemblyRef;
        case CorTableIndex::File: return &db.File;
        case CorTableIndex::ExportedType: return &db.ExportedType;
        case CorTableIndex::ManifestResource: return &db.ManifestResource;
        case CorTableIndex::NestedClass: return &db.NestedClass;
        case CorTableIndex::GenericParam: return &db.GenericParam;
        case CorTableIndex::MethodSpec: return &db.MethodSpec;
        case CorTableIndex::GenericParamConstraint: return &db.GenericParamConstraint;
        default: return nullptr;  // the *Ptr tables and the unused ids
    }
}
} // namespace

std::uint32_t MetadataFile::CorTableRowCount(CorTableIndex table) const {
    if (!IsValid()) return 0;
    const winmd::reader::table_base* t = CorTableLookup(*impl_->db, table);
    return t == nullptr ? 0 : t->size();
}

std::uint32_t MetadataFile::CorTableColumnValue(CorTableIndex table,
    std::uint32_t row, std::uint32_t column) const {
    // The winmd get_value throw for an out-of-range row is the C#
    // BadImageFormatException arm; winmd's own check (row > size) lets a
    // row AT size read past the table's data, so the facade tightens the
    // bound to the table end. An unmodeled table (the *Ptr ids) reads its
    // never-present rows as absent rather than throwing.
    const winmd::reader::table_base* t = IsValid()
        ? CorTableLookup(*impl_->db, table) : nullptr;
    if (t == nullptr) {
        if (row != 0)
            throw std::invalid_argument("Invalid row index");
        return 0;
    }
    if (row >= t->size())
        throw std::invalid_argument("Invalid row index");
    return t->get_value<std::uint32_t>(row, column);
}

MetadataFile::CorTableVersion MetadataFile::CorTableVersionValue(
    CorTableIndex table, std::uint32_t row) const {
    CorTableVersion version;
    if (!IsValid()) return version;
    // winmd merges the four UInt16 version fields into one 8-byte column
    // (column 1 for Assembly, column 0 for AssemblyRef); the little-endian
    // halves are the fields in declaration order.
    std::uint64_t raw = 0;
    if (table == CorTableIndex::Assembly)
        raw = impl_->db->Assembly.get_value<std::uint64_t>(row, 1);
    else if (table == CorTableIndex::AssemblyRef)
        raw = impl_->db->AssemblyRef.get_value<std::uint64_t>(row, 0);
    else
        throw std::invalid_argument("the table carries no version column");
    version.MajorVersion = static_cast<std::uint16_t>(raw & 0xFFFF);
    version.MinorVersion = static_cast<std::uint16_t>((raw >> 16) & 0xFFFF);
    version.BuildNumber = static_cast<std::uint16_t>((raw >> 32) & 0xFFFF);
    version.RevisionNumber = static_cast<std::uint16_t>((raw >> 48) & 0xFFFF);
    return version;
}

std::string MetadataFile::CorString(std::uint32_t heapOffset) const {
    // The nil #Strings offset 0 is the empty string (the heap starts with
    // the nil entry's 0x00 byte); a real offset reads through get_string,
    // which throws for a missing terminator.
    if (heapOffset == 0 || !IsValid()) return {};
    return std::string{impl_->db->get_string(heapOffset)};
}

std::vector<std::uint8_t> MetadataFile::CorBlob(std::uint32_t heapOffset) const {
    // get_blob decodes the compressed length prefix at the offset and
    // returns the payload view; the seek inside throws for an offset past
    // the heap (the BadImageFormatException 'Read out of bounds.' analog).
    // A missing file has no db to reach at all.
    if (!IsValid())
        throw std::invalid_argument("Invalid metadata");
    auto blob = impl_->db->get_blob(heapOffset);
    return std::vector<std::uint8_t>(blob.begin(), blob.end());
}

std::optional<std::array<std::uint8_t, 16>> MetadataFile::CorTryGuid(
    std::uint32_t heapIndex) const {
    // A nil #GUID index is the caller's nil spelling, not Guid.Empty (the
    // MethodBodyReader TryGetGuid maps index 0 to the all-zeros array; the
    // raw surface keeps the nil distinction the C# GuidHandle.IsNil test
    // makes before ever calling GetGuid).
    if (heapIndex == 0 || !IsValid() || !impl_->bodyReader)
        return std::nullopt;
    return impl_->bodyReader->TryGetGuid(heapIndex);
}

} // namespace ILSpy::Decompiler::Metadata
