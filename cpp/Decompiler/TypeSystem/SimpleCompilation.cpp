// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Implementation of `SimpleCompilation` (see SimpleCompilation.hpp). The `Init` body
// resolves the module references against a `SimpleTypeResolveContext(*this)`, dedups
// them, and populates `assemblies_` / `referencedAssemblies_`; the `RootNamespace`
// body lazily builds the `MergedNamespace` via `LazyInit`; the `CreateRootNamespace`
// body builds the namespace array; the three module-getter bodies throw on
// `!initialized_`; and `FindType` / `NameComparer` / `CacheManager` / `TypeSystemOptions`
// / `ToString` return their fixed values.

#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/MergedNamespace.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/Util/LazyInit.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The public ctor -- binds `knownTypeCache_` to `*this` in the member-init list
// (convention (b)), then runs `Init` in the body.
SimpleCompilation::SimpleCompilation(const IModuleReference& mainAssembly,
                                    std::vector<const IModuleReference*> assemblyReferences)
    : knownTypeCache_(*this)
{
    Init(mainAssembly, std::move(assemblyReferences));
}

// The protected default ctor -- binds `knownTypeCache_` to `*this` (convention (b))
// and leaves the compilation uninitialized (no `Init` call); a subclass must call
// `Init` before the compilation is usable.
SimpleCompilation::SimpleCompilation()
    : knownTypeCache_(*this) {}

// The C# `protected void Init(IModuleReference mainAssembly,
// IEnumerable<IModuleReference> assemblyReferences)`:
//   `if (mainAssembly == null) throw new ArgumentNullException(nameof(mainAssembly));`
//   `if (assemblyReferences == null) throw new ArgumentNullException(nameof(assemblyReferences));`
//   `var context = new SimpleTypeResolveContext(this);`
//   `this.mainModule = mainAssembly.Resolve(context);`
//   `List<IModule> assemblies = new List<IModule>();`
//   `assemblies.Add(this.mainModule);`
//   `List<IModule> referencedAssemblies = new List<IModule>();`
//   `foreach (var asmRef in assemblyReferences) {`
//   `    IModule asm;`
//   `    try { asm = asmRef.Resolve(context); }`
//   `    catch (InvalidOperationException) { throw new InvalidOperationException("Tried to ..."); }`
//   `    if (asm != null && !assemblies.Contains(asm)) assemblies.Add(asm);`
//   `    if (asm != null && !referencedAssemblies.Contains(asm)) referencedAssemblies.Add(asm);`
//   `}`
//   `this.assemblies = assemblies.AsReadOnly();`
//   `this.referencedAssemblies = referencedAssemblies.AsReadOnly();`
//   `this.knownTypeCache = new KnownTypeCache(this);`
//   `this.initialized = true;`
// The C++ port mirrors the steps: the two C# null checks are N/A (a reference parameter
// is non-null; the vector is non-null); the `SimpleTypeResolveContext(*this)` threads
// the partially-constructed compilation so the module references can resolve against it;
// the dedup uses `std::find` (pointer equality, the C# `List.Contains` reference
// equality); the C# `InvalidOperationException` catch+rethrow ports to catching
// `std::runtime_error` (the closest C#-`InvalidOperationException` analog -- a runtime
// operational error) and rethrowing `std::runtime_error` with the helpful message. The
// C# `this.knownTypeCache = new KnownTypeCache(this)` is N/A in the C++ port: the
// by-value `knownTypeCache_` member is already bound to `*this` in the ctor's
// member-init list (convention (b)) -- behaviorally equivalent since the cache slots
// start empty and the binding is an impl detail.
void SimpleCompilation::Init(const IModuleReference& mainAssembly,
                            std::vector<const IModuleReference*> assemblyReferences)
{
    SimpleTypeResolveContext context(*this);
    mainModule_ = mainAssembly.Resolve(context);
    std::vector<const IModule*> assemblies;
    assemblies.push_back(mainModule_);
    std::vector<const IModule*> referencedAssemblies;
    for (const IModuleReference* asmRef : assemblyReferences)
    {
        const IModule* asmModule = nullptr;
        try {
            asmModule = asmRef->Resolve(context);
        } catch (const std::runtime_error&) {
            throw std::runtime_error(
                "Tried to initialize compilation with an invalid assembly reference. "
                "(Forgot to load the assembly reference ? - see CecilLoader)");
        }
        if (asmModule != nullptr
            && std::find(assemblies.begin(), assemblies.end(), asmModule) == assemblies.end())
        {
            assemblies.push_back(asmModule);
        }
        if (asmModule != nullptr
            && std::find(referencedAssemblies.begin(), referencedAssemblies.end(),
                         asmModule) == referencedAssemblies.end())
        {
            referencedAssemblies.push_back(asmModule);
        }
    }
    assemblies_ = std::move(assemblies);
    referencedAssemblies_ = std::move(referencedAssemblies);
    initialized_ = true;
}

// The C# `protected virtual INamespace CreateRootNamespace()`:
//   `INamespace[] namespaces = new INamespace[referencedAssemblies.Count + 1];`
//   `namespaces[0] = mainModule.RootNamespace;`
//   `for (int i = 0; i < referencedAssemblies.Count; i++)`
//   `    namespaces[i + 1] = referencedAssemblies[i].RootNamespace;`
//   `return new MergedNamespace(this, namespaces);`
// The C++ port mirrors the array build: the main module's root namespace first, then
// each referenced module's root namespace, then a `MergedNamespace` over them (owned by
// the returned `shared_ptr`, cached in `rootNamespace_`). `const` (convention (f)).
std::shared_ptr<INamespace> SimpleCompilation::CreateRootNamespace() const
{
    std::vector<const INamespace*> namespaces;
    namespaces.reserve(referencedAssemblies_.size() + 1);
    namespaces.push_back(&mainModule_->RootNamespace());
    for (const IModule* module : referencedAssemblies_) {
        namespaces.push_back(&module->RootNamespace());
    }
    return std::make_shared<MergedNamespace>(*this, std::move(namespaces));
}

// --- ICompilation ---

// The C# `IModule MainModule { get { if (!initialized) throw ...; return mainModule; } }`.
const IModule& SimpleCompilation::MainModule() const
{
    if (!initialized_)
        throw std::runtime_error("Compilation isn't initialized yet");
    return *mainModule_;
}

// The C# `IReadOnlyList<IModule> Modules { get { if (!initialized) throw ...; return
// assemblies; } }`.
std::vector<const IModule*> SimpleCompilation::Modules() const
{
    if (!initialized_)
        throw std::runtime_error("Compilation isn't initialized yet");
    return assemblies_;
}

// The C# `IReadOnlyList<IModule> ReferencedModules { get { if (!initialized) throw ...;
// return referencedAssemblies; } }` (the C# property is `ReferencedAssemblies`; the
// `ICompilation` interface names it `ReferencedModules`).
std::vector<const IModule*> SimpleCompilation::ReferencedModules() const
{
    if (!initialized_)
        throw std::runtime_error("Compilation isn't initialized yet");
    return referencedAssemblies_;
}

// The C# `INamespace RootNamespace { get { INamespace ns = LazyInit.VolatileRead(ref
// this.rootNamespace); if (ns != null) return ns; if (!initialized) throw ...; return
// LazyInit.GetOrSet(ref this.rootNamespace, CreateRootNamespace()); } }`.
const INamespace& SimpleCompilation::RootNamespace() const
{
    std::shared_ptr<INamespace> ns = Util::VolatileRead(&rootNamespace_);
    if (ns)
        return *ns;
    if (!initialized_)
        throw std::runtime_error("Compilation isn't initialized yet");
    return *Util::GetOrSet(&rootNamespace_, CreateRootNamespace());
}

// The C# `virtual INamespace GetNamespaceForExternAlias(string alias) { if
// (string.IsNullOrEmpty(alias)) return this.RootNamespace; return null; }`.
const INamespace* SimpleCompilation::GetNamespaceForExternAlias(const std::string& alias) const
{
    if (alias.empty())
        return &this->RootNamespace();
    return nullptr;
}

// The C# `IType FindType(KnownTypeCode typeCode) => knownTypeCache.FindType(typeCode)`.
const IType& SimpleCompilation::FindType(KnownTypeCode typeCode) const
{
    return knownTypeCache_.FindType(typeCode);
}

// The C# `StringComparer NameComparer => StringComparer.Ordinal`.
const StringComparer& SimpleCompilation::NameComparer() const
{
    return StringComparer::Ordinal();
}

// The C# `CacheManager CacheManager => cacheManager`.
const ILSpy::Decompiler::Util::CacheManager& SimpleCompilation::CacheManager() const
{
    return cacheManager_;
}

// The C# `virtual TypeSystemOptions TypeSystemOptions => TypeSystemOptions.Default`.
// Return type + body value GLOBALLY QUALIFIED (convention (h), the D372 name-hiding
// crux: the inherited `ICompilation::TypeSystemOptions()` member function hides the
// namespace-scope `TypeSystemOptions` enum).
::ILSpy::Decompiler::TypeSystem::TypeSystemOptions SimpleCompilation::TypeSystemOptions() const
{
    return ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default;
}

// The C# `override string ToString() => "[" + GetType().Name + " " +
// mainModule.AssemblyName + "]"`. `GetType().Name` for `SimpleCompilation` is the
// literal `"SimpleCompilation"`. A non-virtual diagnostic accessor (convention (i)).
std::string SimpleCompilation::ToString() const
{
    std::string result = "[SimpleCompilation ";
    result += mainModule_->AssemblyName();
    result += "]";
    return result;
}

} // namespace ILSpy::Decompiler::TypeSystem
