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

#include "ILSpyX/LoadedAssemblyExtensions.hpp"

#include "ILSpyX/AssemblyListSnapshot.hpp"
#include "ILSpyX/LoadedAssembly.hpp"

namespace ILSpy::ILSpyX {

// The C# `internal static ConditionalWeakTable<MetadataFile,
// LoadedAssembly> loadedAssemblies` lookup arm, exposed for the
// extensions (the registration happens in LoadedAssembly.cpp; the
// entries die with the wrapper).
const LoadedAssembly* FindLoadedAssembly(
    const Decompiler::Metadata::MetadataFile& file);

LoadedAssembly& GetLoadedAssembly(
    const Decompiler::Metadata::MetadataFile& file)
{
    const LoadedAssembly* loadedAssembly = FindLoadedAssembly(file);
    if (loadedAssembly == nullptr) {
        // The C# ArgumentException message.
        throw std::invalid_argument(
            "The specified file is not associated with a LoadedAssembly!");
    }
    return const_cast<LoadedAssembly&>(*loadedAssembly);
}

std::unique_ptr<Decompiler::Metadata::IAssemblyResolver> GetAssemblyResolver(
    const Decompiler::Metadata::MetadataFile& file, bool loadOnDemand)
{
    return GetLoadedAssembly(file).GetAssemblyResolver(loadOnDemand);
}

std::unique_ptr<Decompiler::Metadata::IAssemblyResolver> GetAssemblyResolver(
    const Decompiler::Metadata::MetadataFile& file,
    const AssemblyListSnapshot& snapshot, bool loadOnDemand)
{
    return GetLoadedAssembly(file).GetAssemblyResolver(snapshot, loadOnDemand);
}

std::shared_ptr<Decompiler::DebugInfo::IDebugInfoProvider> GetDebugInfoOrNull(
    const Decompiler::Metadata::MetadataFile& file)
{
    return GetLoadedAssembly(file).GetDebugInfoOrNull();
}

std::shared_ptr<Decompiler::TypeSystem::ICompilation> GetTypeSystemOrNull(
    const Decompiler::Metadata::MetadataFile& file)
{
    return GetLoadedAssembly(file).GetTypeSystemOrNull();
}

}  // namespace ILSpy::ILSpyX
