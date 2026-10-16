// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The AttributeListBuilder slice's gold tests: every expectation pinned
// against the REAL installed ICSharpCode.Decompiler 11.0 through the
// C:/temp-probe/AlProbe public-API probe (gold_all.txt, regenerated into
// TestFixtures/AttributeGold.hpp by the probe's gen_fixture.py).
//
// The mirror helpers (Quote / RenderArg / AttrLines / EntityLines /
// HasMatrix / Fnv) reproduce the probe's Program.cs shapes byte-for-byte,
// so the section digests and the embedded gold blocks compare directly:
//   * the whole-corpus sweeps -- every TypeDef / Field / Method-parameter
//     GetAttributes() of mscorlib + System.dll + .NET 10 CoreLib, pinned by
//     the FNV-1a-64 digest over the rendered lines (the strongest compact
//     invariant: a single diverging render anywhere flips the digest)
//   * the module-level attribute surfaces -- the assembly/module attribute
//     lists + the InternalsVisibleTo friend list + the [TypeForwardedTo]
//     rows (the GAC facade), compared line-for-line
//   * the curated matrices -- the full renders + the HasAttribute /
//     GetAttribute drives over a fixed KnownAttribute subset (including the
//     KnownAttribute.None drives that pin the C# ArgumentNullException the
//     null-name classification throws)
//   * the custom-options drives -- a TypeSystemOptions.None module over
//     CoreLib pins the IgnoreAttribute gates in the KEEP direction
//     ([Nullable]/[NullableContext]/[IsReadOnly]/[IsByRefLike] appear).

#include <gtest/gtest.h>

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/AttributeListBuilder.hpp"
#include "Decompiler/TypeSystem/Implementation/CustomAttribute.hpp"
#include "Decompiler/TypeSystem/Implementation/DecimalConstantHelper.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "TestFixtures/AttributeGold.hpp"
#include "TestFixtures/MethodAttrGold.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <any>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace TI = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace TM = ::ILSpy::Decompiler::Metadata;

// ---------------------------------------------------------------------------
// The fixture paths + availability gates (the MetadataField_Test precedents).
// ---------------------------------------------------------------------------

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "";
#endif
}

const char* SystemPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "";
#endif
}

const char* CoreLibPath() {
#if defined(_WIN32)
    return "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\"
           "System.Private.CoreLib.dll";
#else
    return "";
#endif
}

const char* FacadePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
#else
    return "";
#endif
}

bool FileExists(const char* path) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }
bool SystemAvailable() { return FileExists(SystemPath()); }
bool CoreLibAvailable() { return FileExists(CoreLibPath()); }
bool FacadeAvailable() { return FileExists(FacadePath()); }

// The tiny.netmodule written from the shared fixture bytes.
std::string WriteTiny() {
    return WriteTinyNetModule();  // global scope (the fixture-header convention)
}

// A module reference resolving to an externally-owned module (the C#
// `PEFile : IModuleReference` shape the gold probe's
// `new SimpleCompilation(new PEFile(...))` drives -- the MetadataField_Test
// fixture precedent).
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
// MetadataField_Test TestCompilation pattern).
class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                    std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

// The probe's NameModule: an IModule answering only AssemblyName (the
// InternalsVisibleTo(name) queries).
class NameModule : public TS::IModule {
public:
    explicit NameModule(std::string name) : name_(std::move(name)) {}

    TS::SymbolKind SymbolKind() const override {
        return TS::SymbolKind::Module;
    }
    std::string Name() const override { return name_; }
    const TS::ICompilation& Compilation() const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    const TM::MetadataFile* MetadataFile() const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    bool IsMainModule() const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    std::string AssemblyName() const override { return name_; }
    TS::Version AssemblyVersion() const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    std::string FullAssemblyName() const override { return name_; }
    std::vector<const TS::IAttribute*> GetAssemblyAttributes()
        const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    std::vector<const TS::IAttribute*> GetModuleAttributes()
        const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    bool InternalsVisibleTo(const TS::IModule&) const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    const TS::INamespace& RootNamespace() const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    const TS::ITypeDefinition* GetTypeDefinition(
        const TS::TopLevelTypeName&) const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    std::vector<const TS::ITypeDefinition*> TopLevelTypeDefinitions()
        const override {
        throw std::runtime_error("NameModule: not implemented");
    }
    std::vector<const TS::ITypeDefinition*> TypeDefinitions()
        const override {
        throw std::runtime_error("NameModule: not implemented");
    }

private:
    std::string name_;
};

// ---------------------------------------------------------------------------
// The render helpers -- each mirrors the probe's Program.cs shapes
// byte-for-byte, so the gold lines compare directly.
// ---------------------------------------------------------------------------

std::string Quote(const std::string& utf8) {
    std::u16string utf16 = ::ILSpy::Decompiler::Util::Utf8ToUtf16(utf8);
    std::string out = "\"";
    char buffer[16];
    for (char16_t ch : utf16) {
        if (ch == u'\\') {
            out += "\\\\";
        } else if (ch == u'"') {
            out += "\\\"";
        } else if (ch == u'\n') {
            out += "\\n";
        } else if (ch == u'\r') {
            out += "\\r";
        } else if (ch == u'\t') {
            out += "\\t";
        } else if (ch < 32 || ch > 126) {
            std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                          static_cast<unsigned>(ch));
            out += buffer;
        } else {
            out += static_cast<char>(ch);
        }
    }
    return out + "\"";
}

// The C# `decimal.ToString(CultureInfo.InvariantCulture)` over the 96-bit
// magnitude (the MetadataField_Test DecimalToString recipe).
std::string DecimalToString(const TI::DecimalConstant& d) {
    std::uint32_t words[3] = {d.lo, d.mid, d.hi};
    std::string digits;
    do {
        std::uint64_t remainder = 0;
        for (int i = 2; i >= 0; i--) {
            std::uint64_t cur = (remainder << 32) | words[i];
            words[i] = static_cast<std::uint32_t>(cur / 1000000000u);
            remainder = cur % 1000000000u;
        }
        bool more = (words[0] | words[1] | words[2]) != 0;
        char group[16];
        if (more) {
            std::snprintf(group, sizeof(group), "%09u",
                          static_cast<unsigned>(remainder));
        } else {
            std::snprintf(group, sizeof(group), "%u",
                          static_cast<unsigned>(remainder));
        }
        digits = group + digits;
    } while ((words[0] | words[1] | words[2]) != 0);
    std::uint32_t scale = d.scale;
    std::string s;
    if (scale == 0) {
        s = digits;
    } else if (digits.size() <= scale) {
        s = "0.";
        s.append(scale - digits.size(), '0');
        s += digits;
    } else {
        s = digits.substr(0, digits.size() - scale);
        s.push_back('.');
        s.append(digits, digits.size() - scale, std::string::npos);
    }
    if (d.isNegative)
        s.insert(s.begin(), '-');
    return s;
}

// The probe's RenderArg: the union of the decoder's box shapes (the
// CustomAttributeDecoder_Test RenderVal) and the constant-box shapes (the
// MetadataField_Test RenderConst).
std::string RenderArg(const std::any& v) {
    if (!v.has_value())
        return "null";
    if (auto b = std::any_cast<bool>(&v))
        return *b ? "bool:True" : "bool:False";
    if (auto s = std::any_cast<std::string>(&v))
        return "str:" + Quote(*s);
    if (auto ch = std::any_cast<char16_t>(&v)) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "char:0x%04X",
                      static_cast<unsigned>(*ch));
        return buf;
    }
    if (auto t = std::any_cast<TS::ITypePtr>(&v)) {
        if (!*t)
            return "null";
        return "type:<" + (*t)->ReflectionName() + ">";
    }
    if (auto arr =
            std::any_cast<std::vector<TS::CustomAttributeTypedArgument>>(&v)) {
        std::string out = "arr:n=" + std::to_string(arr->size()) + ":[";
        for (std::size_t i = 0; i < arr->size(); i++) {
            if (i > 0)
                out += "|";
            out += RenderArg((*arr)[i].Value());
        }
        out += "]";
        return out;
    }
    if (auto boxed = std::any_cast<TS::CustomAttributeTypedArgument>(&v)) {
        std::string type =
            boxed->Type() ? boxed->Type()->ReflectionName() : "<null>";
        return "boxed:type=<" + type + ">:val=" + RenderArg(boxed->Value());
    }
    if (auto f = std::any_cast<float>(&v))
        return "float:"
            + ::ILSpy::Decompiler::Disassembler::FormatRoundTrip(*f);
    if (auto d = std::any_cast<double>(&v))
        return "double:"
            + ::ILSpy::Decompiler::Disassembler::FormatRoundTrip(*d);
    if (auto m = std::any_cast<TI::DecimalConstant>(&v)) {
        char bits[160];
        std::snprintf(bits, sizeof(bits), "dec:%08X,%08X,%08X,%08X:%s",
                      m->lo, m->mid, m->hi, m->Flags(),
                      DecimalToString(*m).c_str());
        return bits;
    }
    if (auto u8 = std::any_cast<std::uint8_t>(&v))
        return "num:Byte:" + std::to_string(*u8);
    if (auto i8 = std::any_cast<std::int8_t>(&v))
        return "num:SByte:" + std::to_string(*i8);
    if (auto i16 = std::any_cast<std::int16_t>(&v))
        return "num:Int16:" + std::to_string(*i16);
    if (auto u16 = std::any_cast<std::uint16_t>(&v))
        return "num:UInt16:" + std::to_string(*u16);
    if (auto i32 = std::any_cast<std::int32_t>(&v))
        return "num:Int32:" + std::to_string(*i32);
    if (auto u32 = std::any_cast<std::uint32_t>(&v))
        return "num:UInt32:" + std::to_string(*u32);
    if (auto i64 = std::any_cast<std::int64_t>(&v))
        return "num:Int64:" + std::to_string(*i64);
    if (auto u64 = std::any_cast<std::uint64_t>(&v))
        return "num:UInt64:" + std::to_string(*u64);
    return "other:";
}

// The C# exception-type name for a port exception (the convention mapping:
// the message distinguishes the .NET type -- the ArgumentNull and
// BadImageFormat texts both map to std::invalid_argument).
std::string ExText(const std::exception& ex) {
    const std::string message = ex.what();
    if (message.rfind("Value cannot be null.", 0) == 0)
        return "EXCEPTION:ArgumentNullException:" + Quote(message);
    if (message.rfind("Format of the executable", 0) == 0
        || message.rfind("Read out of bounds.", 0) == 0
        || message.rfind("Invalid compressed integer.", 0) == 0
        || message.rfind("Invalid serialized string.", 0) == 0)
        return "EXCEPTION:BadImageFormatException:" + Quote(message);
    if (message.rfind("Object reference not set", 0) == 0)
        return "EXCEPTION:NullReferenceException:" + Quote(message);
    return "EXCEPTION:" + Quote(message);
}

// The running FNV-1a-64 over the rendered lines (the probe's digest).
struct Fnv {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    void Line(const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 0x100000001b3ULL;
        }
        h ^= 0xff;
        h *= 0x100000001b3ULL;
    }
    std::string Str() const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llX",
                      static_cast<unsigned long long>(h));
        return buf;
    }
};

// The produced-line sink: every line feeds the digest and the line list.
// When AL_DUMP_DIR is set, the sweep lines also land in a per-tag file for
// diffing against the probe dump (the port-side localization loop).
struct Sink {
    Fnv fnv;
    std::vector<std::string> lines;
    void Out(const std::string& s) {
        lines.push_back(s);
        fnv.Line(s);
    }
    void Dump(const char* tag) const {
        const char* dir = std::getenv("AL_DUMP_DIR");
        if (dir == nullptr)
            return;
        std::string path = std::string(dir) + "/" + tag + ".txt";
        std::FILE* out = std::fopen(path.c_str(), "wb");
        if (out == nullptr)
            return;
        for (const std::string& l : lines)
            std::fprintf(out, "%s\n", l.c_str());
        std::fclose(out);
    }
};

std::string TokenOf(const TS::IEntity& e) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", e.MetadataToken());
    return buf;
}

// The render of one IAttribute (the probe's AttrLines).
void AttrLines(Sink& out, const std::string& prefix,
               const TS::IAttribute* a) {
    std::string ctor;
    try {
        const TS::IMethod* c = a->Constructor();
        ctor = c == nullptr ? "<null>" : c->ReflectionName();
    } catch (const std::exception& ex) {
        // The probe renders "EX=" + the C# exception type name; no gold line
        // reaches this arm (the lazy Constructor scan never throws over the
        // fixtures), so the exact name is unobservable -- the message-carrying
        // form keeps the shape.
        ctor = std::string("EX=") + ex.what();
    }
    auto fixedArgs = a->FixedArguments();
    auto namedArgs = a->NamedArguments();
    out.Out(prefix + "type=<" + a->AttributeType().ReflectionName()
            + ">:ctor=<" + ctor + ">:err="
            + (a->HasDecodeErrors() ? "True" : "False")
            + ":F=" + std::to_string(fixedArgs.size())
            + ":N=" + std::to_string(namedArgs.size()));
    for (std::size_t i = 0; i < fixedArgs.size(); i++) {
        const auto& arg = fixedArgs[i];
        out.Out(prefix + "F" + std::to_string(i) + ":type=<"
                + (arg.Type() ? arg.Type()->ReflectionName()
                              : std::string("<null>"))
                + ">:val=" + RenderArg(arg.Value()));
    }
    for (std::size_t i = 0; i < namedArgs.size(); i++) {
        const auto& arg = namedArgs[i];
        out.Out(prefix + "N" + std::to_string(i) + ":name=" + arg.Name()
                + ":kind="
                + (arg.Kind() == TS::CustomAttributeNamedArgumentKind::Field
                       ? "Field"
                       : "Property")
                + ":type=<"
                + (arg.Type() ? arg.Type()->ReflectionName()
                              : std::string("<null>"))
                + ">:val=" + RenderArg(arg.Value()));
    }
}

// The render of one entity's GetAttributes() (EX line on a throw).
void EntityLines(Sink& out, const std::string& id,
                 const TS::IEntity& e) {
    try {
        std::vector<const TS::IAttribute*> attrs = e.GetAttributes();
        out.Out("E " + id + " n=" + std::to_string(attrs.size()));
        for (std::size_t i = 0; i < attrs.size(); i++)
            AttrLines(out, "A" + std::to_string(i) + ":", attrs[i]);
    } catch (const std::exception& ex) {
        out.Out("E " + id + " " + ExText(ex));
    }
}

// The render of one parameter's GetAttributes() (the IParameter overload --
// IParameter is not an IEntity).
void EntityLines(Sink& out, const std::string& id,
                 const TS::IParameter& p) {
    try {
        std::vector<const TS::IAttribute*> attrs = p.GetAttributes();
        out.Out("E " + id + " n=" + std::to_string(attrs.size()));
        for (std::size_t i = 0; i < attrs.size(); i++)
            AttrLines(out, "A" + std::to_string(i) + ":", attrs[i]);
    } catch (const std::exception& ex) {
        out.Out("E " + id + " " + ExText(ex));
    }
}

// The KnownAttribute subset the HasAttribute/GetAttribute drives cover (the
// probe's AttrSubset, same order).
const std::pair<TS::KnownAttribute, const char*> kAttrSubset[] = {
    {TS::KnownAttribute::None, "None"},
    {TS::KnownAttribute::Serializable, "Serializable"},
    {TS::KnownAttribute::ComImport, "ComImport"},
    {TS::KnownAttribute::SpecialName, "SpecialName"},
    {TS::KnownAttribute::StructLayout, "StructLayout"},
    {TS::KnownAttribute::Flags, "Flags"},
    {TS::KnownAttribute::Obsolete, "Obsolete"},
    {TS::KnownAttribute::Extension, "Extension"},
    {TS::KnownAttribute::CompilerGenerated, "CompilerGenerated"},
    {TS::KnownAttribute::Nullable, "Nullable"},
    {TS::KnownAttribute::NullableContext, "NullableContext"},
    {TS::KnownAttribute::IsReadOnly, "IsReadOnly"},
    {TS::KnownAttribute::IsByRefLike, "IsByRefLike"},
    {TS::KnownAttribute::Dynamic, "Dynamic"},
    {TS::KnownAttribute::TupleElementNames, "TupleElementNames"},
    {TS::KnownAttribute::DecimalConstant, "DecimalConstant"},
    {TS::KnownAttribute::DefaultMember, "DefaultMember"},
    {TS::KnownAttribute::ParamArray, "ParamArray"},
    {TS::KnownAttribute::Optional, "Optional"},
    {TS::KnownAttribute::In, "In"},
    {TS::KnownAttribute::Out, "Out"},
    {TS::KnownAttribute::FieldOffset, "FieldOffset"},
    {TS::KnownAttribute::NonSerialized, "NonSerialized"},
    {TS::KnownAttribute::MarshalAs, "MarshalAs"},
    {TS::KnownAttribute::PermissionSet, "PermissionSet"},
    {TS::KnownAttribute::DefaultParameterValue, "DefaultParameterValue"},
    {TS::KnownAttribute::AssemblyVersion, "AssemblyVersion"},
    {TS::KnownAttribute::InternalsVisibleTo, "InternalsVisibleTo"},
    {TS::KnownAttribute::TypeForwardedTo, "TypeForwardedTo"},
};

void HasMatrix(Sink& out, const std::string& id, const TS::IEntity& e) {
    for (const auto& [ka, name] : kAttrSubset) {
        try {
            out.Out("H " + id + " " + name + "="
                    + std::string(e.HasAttribute(ka) ? "True" : "False"));
        } catch (const std::exception& ex) {
            out.Out("H " + id + " " + name + "=" + ExText(ex));
        }
    }
    for (const auto& [ka, name] : kAttrSubset) {
        const TS::IAttribute* a;
        try {
            a = e.GetAttribute(ka);
        } catch (const std::exception& ex) {
            out.Out("G " + id + " " + name + " " + ExText(ex));
            continue;
        }
        if (a == nullptr) {
            out.Out("G " + id + " " + name + " null");
            continue;
        }
        out.Out("G " + id + " " + name + " found");
        AttrLines(out, "GA:", a);
    }
}

void HasMatrix(Sink& out, const std::string& id, const TS::IParameter& p) {
    // The IParameter HasAttribute/GetAttribute are the TypeSystemExtensions
    // EXTENSIONS (the C# extension methods over the GetAttributes list), not
    // interface members -- the free-function call form.
    for (const auto& [ka, name] : kAttrSubset) {
        try {
            out.Out("H " + id + " " + name + "="
                    + std::string(HasAttribute(p, ka) ? "True" : "False"));
        } catch (const std::exception& ex) {
            out.Out("H " + id + " " + name + "=" + ExText(ex));
        }
    }
    for (const auto& [ka, name] : kAttrSubset) {
        const TS::IAttribute* a;
        try {
            a = TS::GetAttribute(p, ka);
        } catch (const std::exception& ex) {
            out.Out("G " + id + " " + name + " " + ExText(ex));
            continue;
        }
        if (a == nullptr) {
            out.Out("G " + id + " " + name + " null");
            continue;
        }
        out.Out("G " + id + " " + name + " found");
        AttrLines(out, "GA:", a);
    }
}

// The gold-block comparison: split the embedded raw-string gold on '\n' and
// compare line-for-line (the ExpectLinesMatch precedent).
void ExpectLinesMatch(const std::vector<std::string>& produced,
                      const char* gold, const char* what) {
    std::vector<std::string> expected;
    const char* p = gold;
    while (*p != '\0') {
        const char* nl = std::strchr(p, '\n');
        if (nl == nullptr) {
            expected.push_back(p);
            break;
        }
        expected.push_back(std::string(p, nl - p));
        p = nl + 1;
    }
    // The raw-string blocks start and end with a newline around the content
    // (the R"gold(\n...\n)gold" shape) -- drop the leading and trailing
    // empty split elements.
    if (!expected.empty() && expected.back().empty())
        expected.pop_back();
    if (!expected.empty() && expected.front().empty())
        expected.erase(expected.begin());
    ASSERT_EQ(produced.size(), expected.size())
        << what << ": line-count mismatch";
    for (std::size_t i = 0; i < produced.size(); i++) {
        EXPECT_EQ(produced[i], expected[i])
            << what << ": line " << i << ":\n  produced: "
            << produced[i] << "\n  expected: " << expected[i];
    }
}

// ---------------------------------------------------------------------------
// The fixture: the probe's single-module compilations -- one compilation +
// one module per file (mscorlib, System.dll, CoreLib, the facade,
// tiny.netmodule), plus the TypeSystemOptions.None CoreLib variant.
// ---------------------------------------------------------------------------
struct AlFixture {
    TM::MetadataFile mscorlibFile{MscorlibPath()};
    TM::MetadataFile systemFile{SystemPath()};
    TM::MetadataFile coreLibFile{CoreLibPath()};
    TM::MetadataFile facadeFile{FacadePath()};

    TestCompilation mscComp;
    TS::MetadataModule msc{mscComp, &mscorlibFile,
                            TS::TypeSystemOptions::Default};
    FixedModuleRef mscRef{&msc};

    TestCompilation sysComp;
    TS::MetadataModule sys{sysComp, &systemFile,
                            TS::TypeSystemOptions::Default};
    FixedModuleRef sysRef{&sys};

    TestCompilation coreComp;
    TS::MetadataModule core{coreComp, &coreLibFile,
                             TS::TypeSystemOptions::Default};
    FixedModuleRef coreRef{&core};

    TestCompilation facadeComp;
    TS::MetadataModule facade{facadeComp, &facadeFile,
                              TS::TypeSystemOptions::Default};
    FixedModuleRef facadeRef{&facade};

    std::string tinyPath = WriteTiny();
    TM::MetadataFile tinyFile{tinyPath};
    TestCompilation tinyComp;
    TS::MetadataModule tiny{tinyComp, &tinyFile,
                             TS::TypeSystemOptions::Default};
    FixedModuleRef tinyRef{&tiny};

    // The options=None variant over CoreLib (the probe's reflection-built
    // custom-options module; the port's ctor is public).
    TestCompilation optsComp;
    TS::MetadataModule opts{optsComp, &coreLibFile,
                             TS::TypeSystemOptions::None};
    FixedModuleRef optsRef{&opts};

    AlFixture() {
        mscComp.Initialize(mscRef, {});
        sysComp.Initialize(sysRef, {});
        coreComp.Initialize(coreRef, {});
        facadeComp.Initialize(facadeRef, {});
        tinyComp.Initialize(tinyRef, {});
        optsComp.Initialize(optsRef, {});
    }
};

// The sweep driver shared by the three digest tests. `drive` renders one
// entity's lines into the sink.
void TypeSweep(Sink& out, const TS::MetadataModule& module) {
    for (const TS::ITypeDefinition* td : module.TypeDefinitions())
        EntityLines(out, TokenOf(*td), *td);
}

void FieldSweep(Sink& out, const TS::MetadataModule& module) {
    for (const TS::ITypeDefinition* td : module.TypeDefinitions())
        for (const TS::IField* f : td->Fields())
            EntityLines(out, TokenOf(*f), *f);
}

void ParamSweep(Sink& out, const TS::MetadataModule& module) {
    const TS::GetMemberOptions opts =
        TS::GetMemberOptions::IgnoreInheritedMembers
        | TS::GetMemberOptions::ReturnMemberDefinitions;
    for (const TS::ITypeDefinition* td : module.TypeDefinitions()) {
        for (const TS::IMethod* m : td->GetMethods(nullptr, opts)) {
            std::vector<const TS::IParameter*> ps = m->Parameters();
            for (std::size_t pi = 0; pi < ps.size(); pi++) {
                EntityLines(out, TokenOf(*m) + ":" + std::to_string(pi),
                            *ps[pi]);
            }
        }
        for (const TS::IMethod* m : td->GetConstructors(nullptr, opts)) {
            std::vector<const TS::IParameter*> ps = m->Parameters();
            for (std::size_t pi = 0; pi < ps.size(); pi++) {
                EntityLines(out, TokenOf(*m) + ":" + std::to_string(pi),
                            *ps[pi]);
            }
        }
        // The accessor params come with the MetadataProperty/MetadataEvent
        // entity family (the port's GetAccessors deferral) -- not swept.
    }
}

// The render of one method's GetReturnTypeAttributes() (the probe's
// EntityLines over the return-type list -- the IMethod member, not the
// IEntity surface).
void MethodReturnLines(Sink& out, const std::string& id,
                       const TS::IMethod& m) {
    try {
        std::vector<const TS::IAttribute*> attrs =
            m.GetReturnTypeAttributes();
        out.Out("E " + id + " n=" + std::to_string(attrs.size()));
        for (std::size_t i = 0; i < attrs.size(); i++)
            AttrLines(out, "A" + std::to_string(i) + ":", attrs[i]);
    } catch (const std::exception& ex) {
        out.Out("E " + id + " " + ExText(ex));
    }
}

// The method GetAttributes sweep: every MethodDef row in table order (the
// probe's metadata.MethodDefinitions + GetDefinition walk).
void MethodSweep(Sink& out, const TS::MetadataModule& module) {
    std::uint32_t total = module.MetadataFile()->MethodCount();
    for (std::uint32_t row = 1; row <= total; row++) {
        const TS::IMethod* m = module.GetDefinitionMethod(0x06000000u | row);
        if (m == nullptr) {
            out.Out("E " + std::to_string(0x06000000u | row) + " MISSING");
            continue;
        }
        EntityLines(out, TokenOf(*m), *m);
    }
}

// The method GetReturnTypeAttributes sweep (the same row walk).
void MethodReturnSweep(Sink& out, const TS::MetadataModule& module) {
    std::uint32_t total = module.MetadataFile()->MethodCount();
    for (std::uint32_t row = 1; row <= total; row++) {
        const TS::IMethod* m = module.GetDefinitionMethod(0x06000000u | row);
        if (m == nullptr) {
            out.Out("E R:" + std::to_string(0x06000000u | row)
                    + " MISSING");
            continue;
        }
        MethodReturnLines(out, "R:" + TokenOf(*m), *m);
    }
}

// The KnownAttribute subset the METHOD HasAttribute/GetAttribute drives
// cover (the MmAProbe's AttrSubset -- the method-specific list, distinct
// from the entity subset above; same order).
const std::pair<TS::KnownAttribute, const char*> kMethodAttrSubset[] = {
    {TS::KnownAttribute::None, "None"},
    {TS::KnownAttribute::Serializable, "Serializable"},
    {TS::KnownAttribute::SpecialName, "SpecialName"},
    {TS::KnownAttribute::Obsolete, "Obsolete"},
    {TS::KnownAttribute::Extension, "Extension"},
    {TS::KnownAttribute::CompilerGenerated, "CompilerGenerated"},
    {TS::KnownAttribute::Nullable, "Nullable"},
    {TS::KnownAttribute::NullableContext, "NullableContext"},
    {TS::KnownAttribute::DllImport, "DllImport"},
    {TS::KnownAttribute::PreserveSig, "PreserveSig"},
    {TS::KnownAttribute::MethodImpl, "MethodImpl"},
    {TS::KnownAttribute::PermissionSet, "PermissionSet"},
    {TS::KnownAttribute::MarshalAs, "MarshalAs"},
    {TS::KnownAttribute::Optional, "Optional"},
    {TS::KnownAttribute::DefaultParameterValue, "DefaultParameterValue"},
};

void MethodHasMatrix(Sink& out, const std::string& id,
                     const TS::IMethod& m) {
    for (const auto& [ka, name] : kMethodAttrSubset) {
        try {
            out.Out("H " + id + " " + name + "="
                    + std::string(m.HasAttribute(ka) ? "True" : "False"));
        } catch (const std::exception& ex) {
            out.Out("H " + id + " " + name + "=" + ExText(ex));
        }
    }
    for (const auto& [ka, name] : kMethodAttrSubset) {
        const TS::IAttribute* a;
        try {
            a = m.GetAttribute(ka);
        } catch (const std::exception& ex) {
            out.Out("G " + id + " " + name + " " + ExText(ex));
            continue;
        }
        if (a == nullptr) {
            out.Out("G " + id + " " + name + " null");
            continue;
        }
        out.Out("G " + id + " " + name + " found");
        AttrLines(out, "GA:", a);
    }
}

// The curated-section drive: one "C method <tok>" block (the GetAttributes
// render followed by the return-type render), the probe's exact line order.
void CuratedMethodBlock(Sink& out, const TS::MetadataModule& module,
                        const char* label, std::uint32_t token) {
    const TS::IMethod* m = module.GetDefinitionMethod(token);
    if (m == nullptr) {
        out.Out(std::string("C ") + label + " MISSING");
        return;
    }
    out.Out("C " + std::string(label) + " " + TokenOf(*m));
    EntityLines(out, TokenOf(*m), *m);
    MethodReturnLines(out, "R:" + TokenOf(*m), *m);
}

} // namespace

// ---------------------------------------------------------------------------
// The whole-corpus sweeps (the digests).
// ---------------------------------------------------------------------------

TEST(AttributeListBuilderTest, TypeSweepDigestsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;
    TypeSweep(sink, fx.msc);
    sink.Dump("T-msc");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(10641));
    EXPECT_EQ(sink.fnv.Str(), "770527DC9807C162")
        << "T-msc digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    TypeSweep(sink, fx.sys);
    sink.Dump("T-sys");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(5214));
    EXPECT_EQ(sink.fnv.Str(), "B84FDB3DE9E03266")
        << "T-sys digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    TypeSweep(sink, fx.core);
    sink.Dump("T-core");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(7129));
    EXPECT_EQ(sink.fnv.Str(), "7060B70F487A20BF")
        << "T-core digest (lines=" << sink.lines.size() << ")";
}

TEST(AttributeListBuilderTest, FieldSweepDigestsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;
    FieldSweep(sink, fx.msc);
    sink.Dump("F-msc");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(17157));
    EXPECT_EQ(sink.fnv.Str(), "CA342D3385AC1A6E")
        << "F-msc digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    FieldSweep(sink, fx.sys);
    sink.Dump("F-sys");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(16697));
    EXPECT_EQ(sink.fnv.Str(), "9F79643855FF57F9")
        << "F-sys digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    FieldSweep(sink, fx.core);
    sink.Dump("F-core");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(10717));
    EXPECT_EQ(sink.fnv.Str(), "E26DD1E0EFC520CB")
        << "F-core digest (lines=" << sink.lines.size() << ")";
}

TEST(AttributeListBuilderTest, ParamSweepDigestsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;
    ParamSweep(sink, fx.msc);
    sink.Dump("P-msc");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(39286));
    EXPECT_EQ(sink.fnv.Str(), "80649C40C22B9424")
        << "P-msc digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    ParamSweep(sink, fx.sys);
    sink.Dump("P-sys");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(20719));
    EXPECT_EQ(sink.fnv.Str(), "3283E662B85D6607")
        << "P-sys digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    ParamSweep(sink, fx.core);
    sink.Dump("P-core");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(71915));
    EXPECT_EQ(sink.fnv.Str(), "B520A484F0CBAD2D")
        << "P-core digest (lines=" << sink.lines.size() << ")";
}

// ---------------------------------------------------------------------------
// The module-level attribute surfaces (the full-line gold blocks).
// ---------------------------------------------------------------------------

void AsmSection(Sink& out, const TS::MetadataModule& module,
                const char* tag) {
    std::vector<const TS::IAttribute*> asmAttrs =
        module.GetAssemblyAttributes();
    out.Out(std::string("ASM ") + tag + " n="
            + std::to_string(asmAttrs.size()));
    for (std::size_t i = 0; i < asmAttrs.size(); i++)
        AttrLines(out, "AA" + std::to_string(i) + ":", asmAttrs[i]);
    std::vector<const TS::IAttribute*> modAttrs = module.GetModuleAttributes();
    out.Out(std::string("MOD ") + tag + " n="
            + std::to_string(modAttrs.size()));
    for (std::size_t i = 0; i < modAttrs.size(); i++)
        AttrLines(out, "MA" + std::to_string(i) + ":", modAttrs[i]);
    const std::vector<std::string>& ivt = module.GetInternalsVisibleTo();
    out.Out(std::string("IVT ") + tag + " n=" + std::to_string(ivt.size()));
    for (std::size_t i = 0; i < ivt.size(); i++)
        out.Out(std::string("IVT ") + tag + " [" + std::to_string(i) + "] "
                + Quote(ivt[i]));
    for (const char* probe : {"mscorlib", "System", "System.Core",
                              "NO-SUCH", "system"}) {
        NameModule nameModule(probe);
        out.Out(std::string("IVTQ ") + tag + " " + Quote(probe) + "="
                + (module.InternalsVisibleTo(nameModule) ? "True" : "False"));
    }
}

TEST(AttributeListBuilderTest, ModuleAttributesMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;
    AsmSection(sink, fx.msc, "msc");
    ExpectLinesMatch(sink.lines,
                     ILSpy::Tests::AttributeGold::kGoldAsmMscorlib,
                     "A-msc");
    sink = Sink{};
    AsmSection(sink, fx.sys, "sys");
    ExpectLinesMatch(sink.lines, ILSpy::Tests::AttributeGold::kGoldAsmSystem,
                     "A-sys");
    sink = Sink{};
    AsmSection(sink, fx.core, "core");
    ExpectLinesMatch(sink.lines, ILSpy::Tests::AttributeGold::kGoldAsmCoreLib,
                     "A-core");
}

TEST(AttributeListBuilderTest, ModuleAttributesFacadeMatchGold) {
    if (!FacadeAvailable())
        GTEST_SKIP() << "GAC facade fixture not available";
    AlFixture fx;
    Sink sink;
    AsmSection(sink, fx.facade, "facade");
    ExpectLinesMatch(sink.lines, ILSpy::Tests::AttributeGold::kGoldAsmFacade,
                     "A-facade");
}

TEST(AttributeListBuilderTest, ModuleAttributesNetmoduleMatchGold) {
    AlFixture fx;
    Sink sink;
    AsmSection(sink, fx.tiny, "tiny");
    ExpectLinesMatch(sink.lines, ILSpy::Tests::AttributeGold::kGoldAsmTiny,
                     "A-tiny");
}

// ---------------------------------------------------------------------------
// The curated matrices (the full-line gold block).
// ---------------------------------------------------------------------------

TEST(AttributeListBuilderTest, CuratedMatricesMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    AlFixture fx;
    Sink sink;
    for (const char* name : {"System.Exception", "System.String",
                             "System.Math",
                             "System.Runtime.CompilerServices."
                             "ExtensionAttribute",
                             "System.Int32", "System.IComparable`1",
                             "System.EventArgs", "System.ArgIterator",
                             "System.DBNull"}) {
        TS::FullTypeName full(name);
        const TS::ITypeDefinition* td = fx.msc.GetTypeDefinition(
            TS::TopLevelTypeName(full.GetTopLevelTypeName().Namespace(),
                                 full.GetTopLevelTypeName().Name(),
                                 full.GetTopLevelTypeName()
                                     .TypeParameterCount()));
        if (full.IsNested()) {
            // The probe drives IModule.GetTypeDefinition(FullTypeName) --
            // the modules-scan FindType extension's nested walk.
            td = ::ILSpy::Decompiler::TypeSystem::GetTypeDefinition(
                fx.msc, full);
        }
        ASSERT_NE(td, nullptr) << "curated type " << name;
        sink.Out(std::string("C type ") + name);
        EntityLines(sink, TokenOf(*td), *td);
        HasMatrix(sink, std::string("T:") + name, *td);
    }
    TS::FullTypeName eventLogName("System.Diagnostics.EventLog");
    const TS::ITypeDefinition* eventLog =
        ::ILSpy::Decompiler::TypeSystem::GetTypeDefinition(fx.sys,
                                                           eventLogName);
    if (eventLog != nullptr) {
        sink.Out("C type System.Diagnostics.EventLog");
        EntityLines(sink, TokenOf(*eventLog), *eventLog);
        HasMatrix(sink, "T:System.Diagnostics.EventLog", *eventLog);
    }
    for (const char* name : {"System.Diagnostics.EventLogEntry",
                            "System.Net.WebPermission"}) {
        TS::FullTypeName full(name);
        const TS::ITypeDefinition* td =
            ::ILSpy::Decompiler::TypeSystem::GetTypeDefinition(fx.sys, full);
        if (td == nullptr)
            continue;
        sink.Out(std::string("C type ") + name);
        EntityLines(sink, TokenOf(*td), *td);
    }
    // The WIN32_FIND_DATA fields (the marshalled-field arms).
    TS::FullTypeName win32Name("Microsoft.Win32.Win32Native+WIN32_FIND_DATA");
    const TS::ITypeDefinition* win32 =
        ::ILSpy::Decompiler::TypeSystem::GetTypeDefinition(fx.msc, win32Name);
    if (win32 != nullptr) {
        std::vector<const TS::IField*> fields = win32->Fields();
        for (const TS::IField* f : fields) {
            sink.Out(std::string("C field WIN32_FIND_DATA.") + f->Name());
            EntityLines(sink, TokenOf(*f), *f);
        }
        if (!fields.empty()) {
            HasMatrix(sink,
                      std::string("F:") + fields.front()->Name(),
                      *fields.front());
        }
    }
    // The String fields.
    TS::FullTypeName stringName("System.String");
    const TS::ITypeDefinition* stringType =
        ::ILSpy::Decompiler::TypeSystem::GetTypeDefinition(fx.msc, stringName);
    if (stringType != nullptr) {
        for (const char* fname : {"m_firstChar", "m_stringLength"}) {
            std::vector<const TS::IField*> found = stringType->GetFields(
                [fname](const TS::IField* f) {
                    return f->Name() == fname;
                },
                TS::GetMemberOptions::ReturnMemberDefinitions);
            if (found.empty())
                continue;
            const TS::IField* f = found.front();
            sink.Out(std::string("C field String.") + fname);
            EntityLines(sink, TokenOf(*f), *f);
            HasMatrix(sink, std::string("F:String.") + fname, *f);
        }
        // The curated parameters.
        for (const char* mname : {"System.String.Concat",
                                  "System.String.Copy"}) {
            const char* shortName =
                std::strchr(mname, '.') != nullptr
                    ? std::strrchr(mname, '.') + 1
                    : mname;
            std::vector<const TS::IMethod*> methods = stringType->GetMethods(
                [shortName](const TS::IMethod* m) {
                    return m->Name() == shortName;
                },
                TS::GetMemberOptions::IgnoreInheritedMembers
                    | TS::GetMemberOptions::ReturnMemberDefinitions);
            if (methods.empty())
                continue;
            const TS::IMethod* mm = methods.front();
            std::vector<const TS::IParameter*> ps = mm->Parameters();
            for (std::size_t pi = 0; pi < ps.size(); pi++) {
                const TS::IParameter* p = ps[pi];
                sink.Out(std::string("C param ") + mname + " p" + p->Name());
                EntityLines(sink,
                            TokenOf(*mm) + ":" + std::to_string(pi), *p);
                HasMatrix(sink,
                          std::string("P:") + mname + "."
                              + (p->Name().empty() ? std::string("?")
                                                   : p->Name()),
                          *p);
            }
        }
    }
    ExpectLinesMatch(sink.lines, ILSpy::Tests::AttributeGold::kGoldCurated,
                     "C");
}

// ---------------------------------------------------------------------------
// The custom-options drives (the full-line gold block).
// ---------------------------------------------------------------------------

TEST(AttributeListBuilderTest, CustomOptionsModuleMatchesGold) {
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;
    for (const char* name : {"System.String", "System.Span`1",
                             "System.ReadOnlySpan`1", "System.Object",
                             "System.Decimal", "System.IntPtr"}) {
        TS::FullTypeName full(name);
        const TS::ITypeDefinition* td =
            ::ILSpy::Decompiler::TypeSystem::GetTypeDefinition(fx.opts, full);
        if (td == nullptr)
            continue;
        sink.Out(std::string("O type ") + name);
        EntityLines(sink, TokenOf(*td), *td);
        HasMatrix(sink, std::string("O:") + name, *td);
    }
    ExpectLinesMatch(sink.lines, ILSpy::Tests::AttributeGold::kGoldOptions,
                     "O");
}

// ---------------------------------------------------------------------------
// The module attribute-helper contracts (the MakeAttribute/GetAttributeType
// caches + the GetInternalsVisibleTo list).
// ---------------------------------------------------------------------------

TEST(AttributeListBuilderTest, MakeAttributeCachesShareInstances) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    AlFixture fx;
    // The C# LazyInit.GetOrSet identity: the same instance every call.
    std::shared_ptr<TS::IAttribute> a1 = fx.msc.MakeAttribute(
        TS::KnownAttribute::Serializable);
    std::shared_ptr<TS::IAttribute> a2 = fx.msc.MakeAttribute(
        TS::KnownAttribute::Serializable);
    EXPECT_EQ(a1.get(), a2.get());
    EXPECT_EQ(a1->AttributeType().ReflectionName(),
              "System.SerializableAttribute");
    EXPECT_EQ(a1->FixedArguments().size(), static_cast<std::size_t>(0));
    EXPECT_EQ(a1->NamedArguments().size(), static_cast<std::size_t>(0));
    EXPECT_FALSE(a1->HasDecodeErrors());
    // The lazy Constructor scan over the resolved definition (the
    // DefaultAttribute contract): SerializableAttribute's parameterless
    // ctor.
    const TS::IMethod* ctor = a1->Constructor();
    ASSERT_NE(ctor, nullptr);
    EXPECT_EQ(ctor->ReflectionName(),
              "System.SerializableAttribute..ctor");
    // GetAttributeType caches the FindType result identically.
    TS::ITypePtr t1 = fx.msc.GetAttributeType(TS::KnownAttribute::Flags);
    TS::ITypePtr t2 = fx.msc.GetAttributeType(TS::KnownAttribute::Flags);
    EXPECT_EQ(t1.get(), t2.get());
    EXPECT_EQ(t1->ReflectionName(), "System.FlagsAttribute");
}

TEST(AttributeListBuilderTest, InternalsVisibleToListMatchesGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    AlFixture fx;
    const std::vector<std::string>& ivt = fx.msc.GetInternalsVisibleTo();
    // mscorlib's nine [InternalsVisibleTo] rows, short-named.
    ASSERT_EQ(ivt.size(), static_cast<std::size_t>(9));
    EXPECT_EQ(ivt[0], "System");
    EXPECT_EQ(ivt[1], "System.Core");
    EXPECT_EQ(ivt[2], "System.Numerics");
    EXPECT_EQ(ivt[3], "System.Reflection.Context");
    EXPECT_EQ(ivt[4], "System.Runtime.WindowsRuntime");
    EXPECT_EQ(ivt[5], "System.Runtime.WindowsRuntime.UI.Xaml");
    EXPECT_EQ(ivt[6], "WindowsBase");
    EXPECT_EQ(ivt[7], "PresentationCore");
    EXPECT_EQ(ivt[8], "PresentationFramework");
    // The list is cached (the C# LazyInit field -- the same vector).
    const std::vector<std::string>& ivt2 = fx.msc.GetInternalsVisibleTo();
    EXPECT_EQ(&ivt, &ivt2);
    // The queries: the self arm, the ordinal-ignore-case match, the miss.
    EXPECT_TRUE(fx.msc.InternalsVisibleTo(fx.msc));
    NameModule systemModule("System");
    EXPECT_TRUE(fx.msc.InternalsVisibleTo(systemModule));
    NameModule lowerModule("system");
    EXPECT_TRUE(fx.msc.InternalsVisibleTo(lowerModule));
    NameModule missingModule("NO-SUCH");
    EXPECT_FALSE(fx.msc.InternalsVisibleTo(missingModule));
    // tiny.netmodule: not an assembly -- the empty list.
    EXPECT_EQ(fx.tiny.GetInternalsVisibleTo().size(),
              static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// The MetadataMethod attribute slice (the MmAProbe gold): the method
// GetAttributes / GetReturnTypeAttributes sweeps and the curated drives.
// ---------------------------------------------------------------------------

TEST(AttributeListBuilderTest, MethodSweepDigestsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;
    MethodSweep(sink, fx.msc);
    sink.Dump("MM-msc");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(52412));
    EXPECT_EQ(sink.fnv.Str(), "7BF24123E426D092")
        << "MM-msc digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    MethodSweep(sink, fx.sys);
    sink.Dump("MM-sys");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(23575));
    EXPECT_EQ(sink.fnv.Str(), "A1892EABD7C1EDA1")
        << "MM-sys digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    MethodSweep(sink, fx.core);
    sink.Dump("MM-core");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(68197));
    EXPECT_EQ(sink.fnv.Str(), "30EA44D203D0FE20")
        << "MM-core digest (lines=" << sink.lines.size() << ")";
}

TEST(AttributeListBuilderTest, MethodReturnSweepDigestsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;
    MethodReturnSweep(sink, fx.msc);
    sink.Dump("RR-msc");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(29652));
    EXPECT_EQ(sink.fnv.Str(), "64386D20900F4E6D")
        << "RR-msc digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    MethodReturnSweep(sink, fx.sys);
    sink.Dump("RR-sys");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(18184));
    EXPECT_EQ(sink.fnv.Str(), "A0DDFB338CB69749")
        << "RR-sys digest (lines=" << sink.lines.size() << ")";
    sink = Sink{};
    MethodReturnSweep(sink, fx.core);
    sink.Dump("RR-core");
    EXPECT_EQ(sink.lines.size(), static_cast<std::size_t>(42763));
    EXPECT_EQ(sink.fnv.Str(), "C55C221A7D1781B2")
        << "RR-core digest (lines=" << sink.lines.size() << ")";
}

TEST(AttributeListBuilderTest, MethodCuratedMatchesGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    AlFixture fx;
    Sink sink;

    // The pinvoke fixture set (the census-picked shapes covering every
    // DllImport named-arg arm reachable on the local corpora).
    for (std::uint32_t tok :
         {0x0600001Au, 0x0600001Du, 0x06000026u, 0x06000153u, 0x060064C5u,
          0x0600001Bu, 0x06002EA4u, 0x06005021u}) {
        CuratedMethodBlock(sink, fx.msc, "method", tok);
    }
    if (const TS::IMethod* pinvoke =
            fx.msc.GetDefinitionMethod(0x0600001Au))
        MethodHasMatrix(sink, "M:pinvoke", *pinvoke);

    // System.dll's ThrowOnUnmappableChar pinvokes (GetAttributes only --
    // the probe's block shape).
    for (std::uint32_t tok : {0x060040ADu, 0x060040BAu}) {
        const TS::IMethod* m = fx.sys.GetDefinitionMethod(tok);
        ASSERT_NE(m, nullptr);
        sink.Out("C sys method " + TokenOf(*m));
        EntityLines(sink, TokenOf(*m), *m);
    }

    // The MethodImpl / PreserveSig synthetic-row fixtures.
    for (std::uint32_t tok :
         {0x060000EFu, 0x06001D8Fu, 0x06004090u, 0x060040EAu, 0x06000007u}) {
        CuratedMethodBlock(sink, fx.msc, "method", tok);
    }
    {
        // sys .ctor with impl 0x1003: InternalCall + MethodCodeType Runtime.
        const TS::IMethod* m = fx.sys.GetDefinitionMethod(0x06000277u);
        ASSERT_NE(m, nullptr);
        sink.Out("C sys method " + TokenOf(*m));
        EntityLines(sink, TokenOf(*m), *m);
    }

    // The by-name lookups (String.Copy, Object.ToString, Math.Abs).
    const TS::GetMemberOptions opts =
        TS::GetMemberOptions::IgnoreInheritedMembers
        | TS::GetMemberOptions::ReturnMemberDefinitions;
    for (const auto& [tname, tpc, name, mname] :
         std::vector<std::tuple<const char*, int, const char*,
                                const char*>>{
             {"System", 0, "String", "Copy"},
             {"System", 0, "Object", "ToString"},
             {"System", 0, "Math", "Abs"}}) {
        const TS::ITypeDefinition* td =
            fx.msc.GetTypeDefinition(TS::TopLevelTypeName(tname, name, tpc));
        ASSERT_NE(td, nullptr);
        const TS::IMethod* m = nullptr;
        for (const TS::IMethod* cand : td->GetMethods(
                 [&mname](const TS::IMethod* mm) {
                     return mm->Name() == mname;
                 },
                 opts)) {
            m = cand;
            break;
        }
        ASSERT_NE(m, nullptr) << tname << '.' << name << '.' << mname;
        sink.Out("C method " + TokenOf(*m) + " " + tname + "." + name
                 + "." + mname);
        EntityLines(sink, TokenOf(*m), *m);
        MethodReturnLines(sink, "R:" + TokenOf(*m), *m);
    }

    // The SymbolKind-shape fixtures (a .ctor, the Destructor/interface-Method
    // Finalize pair, an op_ operator).
    for (std::uint32_t tok :
         {0x060004E5u, 0x06000231u, 0x06000011u, 0x06000D78u}) {
        CuratedMethodBlock(sink, fx.msc, "method", tok);
    }
    {
        // The HasAttribute/GetAttribute matrix over String.Copy.
        const TS::ITypeDefinition* td = fx.msc.GetTypeDefinition(
            TS::TopLevelTypeName("System", "String", 0));
        ASSERT_NE(td, nullptr);
        const TS::IMethod* m = nullptr;
        for (const TS::IMethod* cand : td->GetMethods(
                 [](const TS::IMethod* mm) {
                     return mm->Name() == "Copy";
                 },
                 opts)) {
            m = cand;
            break;
        }
        ASSERT_NE(m, nullptr);
        MethodHasMatrix(sink, "M:plain", *m);
    }

    // System.dll's EventLog.WriteEntry overloads (the first three).
    {
        const TS::ITypeDefinition* td = fx.sys.GetTypeDefinition(
            TS::TopLevelTypeName("System.Diagnostics", "EventLog", 0));
        ASSERT_NE(td, nullptr);
        std::size_t taken = 0;
        for (const TS::IMethod* m : td->GetMethods(
                 [](const TS::IMethod* mm) {
                     return mm->Name() == "WriteEntry";
                 },
                 opts)) {
            if (taken == 3)
                break;
            taken++;
            sink.Out("C method " + TokenOf(*m)
                     + " System.Diagnostics.EventLog.WriteEntry");
            EntityLines(sink, TokenOf(*m), *m);
            MethodReturnLines(sink, "R:" + TokenOf(*m), *m);
        }
        ASSERT_EQ(taken, static_cast<std::size_t>(3));
    }

    // CoreLib: the RuntimeAsync impl bits (0x2000 -- the SRMHacks
    // MethodImplAsync mask this engine never clears under default
    // options), AggressiveOptimization 0x200, and the return-value
    // custom-attribute rows (the only local corpus carrying them).
    for (std::uint32_t tok :
         {0x060080CAu, 0x060080CCu, 0x060081C6u, 0x0600028Du, 0x060002C0u,
          0x06000020u, 0x0600030Fu}) {
        CuratedMethodBlock(sink, fx.core, "core", tok);
    }

    // mscorlib's return-value marshalling row (IsWow64Process: the [return:
    // MarshalAs] GetReturnTypeAttributes shape).
    CuratedMethodBlock(sink, fx.msc, "method", 0x06000029u);

    // The gold comparison (281 lines).
    ASSERT_EQ(sink.lines.size(),
        ILSpy::Tests::MethodAttrGold::kGoldMethodCuratedCount);
    for (std::size_t i = 0; i < sink.lines.size(); i++) {
        EXPECT_EQ(sink.lines[i],
            ILSpy::Tests::MethodAttrGold::kGoldMethodCurated[i])
            << "MethodCuratedMatchesGold: line " << i << ":\n  produced: "
            << sink.lines[i] << "\n  expected: "
            << ILSpy::Tests::MethodAttrGold::kGoldMethodCurated[i];
    }
}
