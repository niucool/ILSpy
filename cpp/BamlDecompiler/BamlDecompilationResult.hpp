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

// Port of ICSharpCode.BamlDecompiler/BamlDecompilationResult.cs (Siegfried
// Pammer, 2021, MIT): the result one `XamlDecompiler.Decompile` run returns
// -- the finished XAML document, the `x:Class` type name of the first
// main-module class element the XClass pass rewrote (null when no
// main-module element was in the document), the assembly full names the
// BAML's `AssemblyIdMap` rows carry, and the generated-member tokens the
// ConnectionId pass collects (empty until that pass lands -- the port's
// documented deferral).
//
// C#-to-C++ porting decisions:
//  * The C# `XDocument Xaml` reference (the GC roots the document the
//    caller keeps) ports to `std::shared_ptr<Xml::XDocument>` (the DOM
//    GC-ownership convention); a null is not constructible (Decompile
//    always builds the document).
//  * The C# `FullTypeName? TypeName` nullable struct ports to
//    `std::optional<FullTypeName>`.
//  * The C# `List<EntityHandle> GeneratedMembers` ports to raw tokens
//    (the iteration-13 IDebugInfoProvider convention -- an `EntityHandle`
//    is its 32-bit metadata token).
//  * The ctor takes its collections by value and copies (the C# `ToList()`
//    of the IEnumerable parameters).

#pragma once

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/Xml/XDocument.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::BamlDecompiler {

// The BAML decompiler's C# `using System.Xml.Linq` -- an alias to the port's
// stand-in namespace (the BamlDecompiler tree is not nested in
// ILSpy::Decompiler, so its Xml references need the alias).
namespace Xml = ::ILSpy::Decompiler::Xml;

// The C# `public class BamlDecompilationResult`.
class BamlDecompilationResult {
public:
    // The C# ctor `BamlDecompilationResult(XDocument xaml, FullTypeName?
    // typeName, IEnumerable<string> assemblyReferences, IEnumerable<
    // EntityHandle> generatedMembers)` (the value parameters standing in
    // for the enumerated sources; the port moves them in).
    BamlDecompilationResult(std::shared_ptr<Xml::XDocument> xaml,
        std::optional<ILSpy::Decompiler::TypeSystem::FullTypeName> typeName,
        std::vector<std::string> assemblyReferences,
        std::vector<std::uint32_t> generatedMembers)
        : xaml_(std::move(xaml)),
          typeName_(std::move(typeName)),
          assemblyReferences_(std::move(assemblyReferences)),
          generatedMembers_(std::move(generatedMembers))
    {
    }

    // The C# `XDocument Xaml { get; }`.
    const std::shared_ptr<Xml::XDocument>& Xaml() const { return xaml_; }

    // The C# `FullTypeName? TypeName { get; }`.
    const std::optional<ILSpy::Decompiler::TypeSystem::FullTypeName>& TypeName() const
    {
        return typeName_;
    }

    // The C# `List<string> AssemblyReferences { get; }`.
    const std::vector<std::string>& AssemblyReferences() const { return assemblyReferences_; }

    // The C# `List<EntityHandle> GeneratedMembers { get; }` (the raw-token
    // convention -- see the header porting decisions).
    const std::vector<std::uint32_t>& GeneratedMembers() const { return generatedMembers_; }

private:
    std::shared_ptr<Xml::XDocument> xaml_;
    std::optional<ILSpy::Decompiler::TypeSystem::FullTypeName> typeName_;
    std::vector<std::string> assemblyReferences_;
    std::vector<std::uint32_t> generatedMembers_;
};

} // namespace ILSpy::BamlDecompiler
