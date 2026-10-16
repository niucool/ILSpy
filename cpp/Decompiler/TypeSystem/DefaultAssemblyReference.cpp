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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `DefaultAssemblyReference` (see DefaultAssemblyReference.hpp).
// The ctor extracts the short name (the prefix before the first `','`) from the
// full assembly name. The `Resolve` body matches `shortName_` case-insensitively
// against `context.CurrentModule` first, then each module in
// `context.Compilation.Modules`, returning the matched `IModule` or null. The
// `CurrentModuleReference::Resolve` body returns `context.CurrentModule` or
// throws `std::invalid_argument` when it is null.

#include "Decompiler/TypeSystem/DefaultAssemblyReference.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `DefaultAssemblyReference(string assemblyName)` -- finds the first `','`
// in the full assembly name and keeps the prefix as the short name (or the whole
// string when no comma is present). The C# `assemblyName.IndexOf(',')` returns -1
// when there is no comma (or the string is null); the C++ `std::string::find(',')`
// returns `std::string::npos` in that case, so the `pos != npos` test mirrors the
// C# `pos >= 0` test. The C# `Substring(0, pos)` copies the prefix; the C++
// `substr(0, pos)` likewise. When no comma, the C# `shortName = assemblyName`
// copies the whole string; the C++ `std::move(assemblyName)` moves it (the `find`
// has already read the original before the move).
DefaultAssemblyReference::DefaultAssemblyReference(std::string assemblyName)
{
    auto pos = assemblyName.find(',');
    if (pos != std::string::npos)
        shortName_ = assemblyName.substr(0, pos);
    else
        shortName_ = std::move(assemblyName);
}

// The C# `IModule Resolve(ITypeResolveContext context)` -- matches `shortName_`
// case-insensitively against `context.CurrentModule` first, then each module in
// `context.Compilation.Modules`. The C# `string.Equals(shortName,
// current.AssemblyName, StringComparison.OrdinalIgnoreCase)` ports to
// `StringComparer::OrdinalIgnoreCase().Equals(shortName_, current->AssemblyName())`
// (the D398 ASCII case fold, the faithful C++ counterpart of
// `StringComparison.OrdinalIgnoreCase` for the ASCII identifier range assembly names
// live in). The C# `IModule?` nullable return ports to a nullable `const IModule*`;
// `nullptr` when no module matches (the "module not found" case).
const IModule* DefaultAssemblyReference::Resolve(
    const ITypeResolveContext& context) const
{
    const IModule* current = context.CurrentModule();
    if (current != nullptr
        && StringComparer::OrdinalIgnoreCase().Equals(shortName_, current->AssemblyName()))
    {
        return current;
    }
    for (const IModule* module : context.Compilation().Modules())
    {
        if (StringComparer::OrdinalIgnoreCase().Equals(shortName_, module->AssemblyName()))
            return module;
    }
    return nullptr;
}

// The C# `static readonly IModuleReference CurrentAssembly = new
// CurrentModuleReference()` -- a function-local static singleton (the
// Meyers-singleton pattern, the D398 `StringComparer::Ordinal` precedent). Returns
// `const IModuleReference&` to the single `CurrentModuleReference` instance; the
// `CurrentModuleReference` is a private nested class, so this static accessor (a
// member of the enclosing `DefaultAssemblyReference`) can construct it.
const IModuleReference& DefaultAssemblyReference::CurrentAssembly()
{
    static const CurrentModuleReference instance;
    return instance;
}

// The C# `sealed class CurrentModuleReference : IModuleReference` -- its `Resolve`
// returns `context.CurrentModule` or throws `ArgumentException` when the current
// module is null. The C# `ArgumentException` (with the "cannot be resolved in the
// compilation's global type resolve context" message) ports to
// `std::invalid_argument` (the standard-library counterpart of an argument-contract
// violation); the message is ported verbatim. The C# `IModule` (non-null after the
// null check) return ports to `const IModule*` (the `IModuleReference::Resolve`
// nullable-pointer return contract; the throw guarantees non-null when it returns).
const IModule* DefaultAssemblyReference::CurrentModuleReference::Resolve(
    const ITypeResolveContext& context) const
{
    const IModule* module = context.CurrentModule();
    if (module == nullptr)
    {
        throw std::invalid_argument(
            "A reference to the current assembly cannot be resolved in the "
            "compilation's global type resolve context.");
    }
    return module;
}

} // namespace ILSpy::Decompiler::TypeSystem
