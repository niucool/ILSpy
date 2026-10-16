// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The XamlDecompiler port body (the header carries the porting decisions):
// the constructor family over CreateTypeSystemFromFile/LoadPEFile, the
// static rewrite-pass array, and the byte-span Decompile composition.

#include "BamlDecompiler/XamlDecompiler.hpp"

#include "BamlDecompiler/Baml/BamlReader.hpp"
#include "BamlDecompiler/BamlDecompilerTypeSystem.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/Rewrite/AttributeRewritePass.hpp"
#include "BamlDecompiler/Rewrite/ConnectionIdRewritePass.hpp"
#include "BamlDecompiler/Rewrite/DocumentRewritePass.hpp"
#include "BamlDecompiler/Rewrite/MarkupExtensionRewritePass.hpp"
#include "BamlDecompiler/Rewrite/XClassRewritePass.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/PEReaderParse.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "Decompiler/Xml/XDocument.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::BamlDecompiler {

namespace {

namespace fs = std::filesystem;

namespace Util = ::ILSpy::Decompiler::Util;

// The standard .NET NullReferenceException message (the XmlnsDictionary
// convention; the KnownThings.cpp sibling copy).
constexpr const char* kNreMessage = "Object reference not set to an instance of an object.";

// The C# ArgumentException text the Uncached check throws.
constexpr const char* kUncachedMessage =
    "Cannot use an uncached type system in the decompiler.";

// The .NET 10 failure-arm texts the LoadPEFile chain reproduces.
constexpr const char* kFileNotFoundMessage = "Could not find file '";
constexpr const char* kDirectoryNotFoundMessage =
    "Could not find a part of the path '";
constexpr const char* kMetadataOverflowMessage =
    "Arithmetic operation resulted in an overflow.";

// The C# `static readonly IRewritePass[] rewritePasses` (the one static
// field -- XClassRewritePass, MarkupExtensionRewritePass,
// AttributeRewritePass, ConnectionIdRewritePass, DocumentRewritePass, in
// order).
const std::vector<std::unique_ptr<Rewrite::IRewritePass>>& RewritePasses()
{
    static const std::vector<std::unique_ptr<Rewrite::IRewritePass>> passes = [] {
        std::vector<std::unique_ptr<Rewrite::IRewritePass>> v;
        v.push_back(std::make_unique<Rewrite::XClassRewritePass>());
        v.push_back(std::make_unique<Rewrite::MarkupExtensionRewritePass>());
        v.push_back(std::make_unique<Rewrite::AttributeRewritePass>());
        v.push_back(std::make_unique<Rewrite::ConnectionIdRewritePass>());
        v.push_back(std::make_unique<Rewrite::DocumentRewritePass>());
        return v;
    }();
    return passes;
}

// The OS-boundary conversion (the resolver's Utf8ToWide convention: a UTF-8
// path must become an fs::path through the wide form -- the ANSI-page
// mapping would mangle non-ASCII paths).
#if defined(_WIN32)
fs::path ToFsPath(const std::string& utf8)
{
    const std::u16string wide = Util::Utf8ToUtf16(utf8);
    return fs::path(std::wstring(wide.begin(), wide.end()));
}
#else
fs::path ToFsPath(const std::string& utf8)
{
    return fs::path(utf8);
}
#endif

// The C# `Directory.Exists(parent)` probe the DirectoryNotFoundException arm
// needs (the file-not-found vs directory-not-found distinction).
bool ParentDirectoryExists(const std::string& fileName)
{
    const std::size_t pos = fileName.find_last_of("\\/");
    if (pos == std::string::npos) return false;
    const std::string parent = fileName.substr(0, pos);
    if (parent.empty()) return false;
    std::error_code ec;
    return fs::is_directory(ToFsPath(parent), ec);
}

} // namespace

// The C# `static PEFile LoadPEFile(string fileName, BamlDecompilerSettings
// settings)` -- the `new PEFile(fileName, new FileStream(fileName,
// FileMode.Open, FileAccess.Read), streamOptions: PrefetchEntireImage,
// metadataOptions: None)` chain (the settings parameter is unused in the C#).
std::unique_ptr<ILSpy::Decompiler::Metadata::MetadataFile>
XamlDecompiler::LoadPEFile(const std::string& fileName)
{
    // The C# `new FileStream(fileName, FileMode.Open, FileAccess.Read)`:
    // an unopenable path is the FileNotFoundException arm when the parent
    // exists, the DirectoryNotFoundException arm when it does not.
    {
        std::ifstream probe(fileName, std::ios::binary);
        if (!probe.is_open()) {
            if (ParentDirectoryExists(fileName))
                throw std::runtime_error(std::string(kFileNotFoundMessage)
                                         + fileName + "'.");
            throw std::runtime_error(std::string(kDirectoryNotFoundMessage)
                                     + fileName + "'.");
        }
    }

    // The C# `new PEReader(stream, PEStreamOptions.PrefetchEntireImage)` +
    // the `MetadataFile` ctor's `reader.HasMetadata` test: the .NET 10
    // PEHeaders eager parse over the file bytes, throwing the exact
    // BadImageFormatException message of every malformed arm.
    {
        std::ifstream input(fileName, std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        ILSpy::Decompiler::Metadata::MetadataBlock block =
            ILSpy::Decompiler::Metadata::ParsePEReaderHeaders(bytes);
        if (block.size == 0)
            throw ILSpy::Decompiler::Metadata::MetadataFileNotSupportedException(
                "PE file does not contain any managed metadata.");
    }

    // The metadata parse: the C# `GetMetadataReader` inside the MetadataFile
    // constructor -- the port's never-throwing constructor reports
    // IsValid(), and the representative OverflowException message is the
    // real engine's corrupt-table-root arm (the documented divergence for
    // the other corrupt-metadata shapes).
    auto file = std::make_unique<ILSpy::Decompiler::Metadata::MetadataFile>(
        fileName);
    if (!file->IsValid())
        throw std::overflow_error(kMetadataOverflowMessage);
    return file;
}

// The C# `static BamlDecompilerTypeSystem CreateTypeSystemFromFile(string
// fileName, BamlDecompilerSettings settings)`.
XamlDecompiler::CreateTypeSystemFromFileResult
XamlDecompiler::CreateTypeSystemFromFile(const std::string& fileName,
                                         const BamlDecompilerSettings& settings)
{
    CreateTypeSystemFromFileResult parts;
    parts.file = LoadPEFile(fileName);
    parts.resolver = std::make_unique<
        ILSpy::Decompiler::Metadata::UniversalAssemblyResolver>(
        fileName, settings.ThrowOnAssemblyResolveErrors(),
        ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(*parts.file),
        ILSpy::Decompiler::Metadata::DetectRuntimePack(*parts.file),
        ILSpy::Decompiler::Metadata::PEStreamOptions::PrefetchMetadata,
        ILSpy::Decompiler::Metadata::MetadataReaderOptions::None);
    parts.typeSystem = std::make_unique<BamlDecompilerTypeSystem>(
        *parts.file, *parts.resolver);
    return parts;
}

// The C# ctor-4 body over a concrete type system.
void XamlDecompiler::Init(const BamlDecompilerTypeSystem& typeSystem,
                          const BamlDecompilerSettings* settings)
{
    typeSystem_ = &typeSystem;
    settings_ = settings;
    module_ = &typeSystem.MainModule();
    if ((module_->TypeSystemOptions()
            & ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Uncached)
        != ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
        throw std::invalid_argument(kUncachedMessage);
}

// The out-of-line default: the implicit destructor would need the complete
// unique_ptr member types in every consuming TU (the header convention
// note).
XamlDecompiler::~XamlDecompiler() = default;

// The interface-typed stand-in ctor (the header note): no owning member is
// initialized, so no exception specification over the incomplete types.
XamlDecompiler::XamlDecompiler(
    const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
    const BamlDecompilerSettings* settings)
    : typeSystem_(&typeSystem), settings_(settings)
{
}

XamlDecompiler::XamlDecompiler(const std::string& fileName,
                               const BamlDecompilerSettings& settings)
{
    // The C# chains through `CreateTypeSystemFromFile(fileName, settings)`;
    // the port owns the pieces the C# GC roots.
    CreateTypeSystemFromFileResult parts =
        CreateTypeSystemFromFile(fileName, settings);
    ownedResolver_ = std::move(parts.resolver);
    ownedFile_ = std::move(parts.file);
    ownedTypeSystem_ = std::move(parts.typeSystem);
    Init(*ownedTypeSystem_, &settings);
}

XamlDecompiler::XamlDecompiler(
    const std::string& fileName,
    const ILSpy::Decompiler::Metadata::IAssemblyResolver& assemblyResolver,
    const BamlDecompilerSettings* settings)
{
    // The C# chains through `LoadPEFile(fileName, settings)`; the port owns
    // the loaded file and the type system (the caller's resolver keeps the
    // resolved referenced files alive).
    ownedFile_ = LoadPEFile(fileName);
    ownedTypeSystem_ = std::make_unique<BamlDecompilerTypeSystem>(
        *ownedFile_, assemblyResolver);
    Init(*ownedTypeSystem_, settings);
}

XamlDecompiler::XamlDecompiler(
    const ILSpy::Decompiler::Metadata::MetadataFile& module,
    const ILSpy::Decompiler::Metadata::IAssemblyResolver& assemblyResolver,
    const BamlDecompilerSettings* settings)
{
    ownedTypeSystem_ =
        std::make_unique<BamlDecompilerTypeSystem>(module, assemblyResolver);
    Init(*ownedTypeSystem_, settings);
}

XamlDecompiler::XamlDecompiler(const BamlDecompilerTypeSystem& typeSystem,
                               const BamlDecompilerSettings* settings)
{
    Init(typeSystem, settings);
}

// The C# `public BamlDecompilationResult Decompile(Stream stream)`.
BamlDecompilationResult XamlDecompiler::Decompile(const std::uint8_t* data, std::size_t size)
{
    auto document = Baml::ReadDocument(data, size);
    auto ctx = XamlContext::Construct(*typeSystem_, document, settings_);

    // The C# `HandlerMap.LookupHandler(ctx.RootNode.Type)`: an empty
    // document makes `BamlNode.Parse` return null and the `RootNode.Type`
    // deref is the NullReferenceException (the port's explicit throw --
    // the header porting decisions).
    Baml::BamlBlockNode* rootNode = ctx->RootNode();
    if (rootNode == nullptr)
        throw std::runtime_error(kNreMessage);
    IHandler* handler = HandlerMap::LookupHandler(rootNode->Type());

    auto elem = handler->Translate(*ctx, *rootNode, nullptr);
    // The C# `elem.Xaml.Element` deref (a root handler contributing no
    // element -- unreachable through the builtin handlers, whose root is
    // always a block record with a handler that returns an element).
    if (elem == nullptr)
        throw std::runtime_error(kNreMessage);

    auto xaml = std::make_shared<Xml::XDocument>();
    xaml->Add(elem->Xaml.Element);

    for (const auto& pass : RewritePasses())
        pass->Run(*ctx, *xaml);

    // The C# `ctx.Baml.AssemblyIdMap.Select(a => a.Value.AssemblyFullName)`
    // -- the map's values in id order (the index==id vector iteration).
    std::vector<std::string> assemblyReferences;
    for (const Baml::AssemblyInfoRecord* rec : ctx->Baml().AssemblyIdMap)
        assemblyReferences.push_back(rec->AssemblyFullName);

    // The C# `ctx.XClassNames.FirstOrDefault() is string s ?
    // (FullTypeName?)new FullTypeName(s) : null`.
    std::optional<ILSpy::Decompiler::TypeSystem::FullTypeName> typeName;
    if (!ctx->XClassNames().empty())
        typeName = ILSpy::Decompiler::TypeSystem::FullTypeName(ctx->XClassNames().front());

    return BamlDecompilationResult(std::move(xaml), std::move(typeName),
        std::move(assemblyReferences), ctx->GeneratedMembers());
}

} // namespace ILSpy::BamlDecompiler
