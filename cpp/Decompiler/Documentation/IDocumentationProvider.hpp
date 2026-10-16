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

// Port of the `IDocumentationProvider` interface in
// ICSharpCode.Decompiler/Documentation/XmlDocumentationProvider.cs -- the XML
// documentation provider contract. The concrete `XmlDocumentationProvider` (the
// in-memory `.xml` index + reader) stays with a later Decompiler slice; the port
// carries only the interface the `AddXmlDocumentationTransform` reads.
//
// The C# `string GetDocumentation(IEntity entity)` returns a nullable reference
// (a missing entry is null), so the port returns `std::optional<std::string>`.

#pragma once

#include <optional>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {
class IEntity;
}

namespace ILSpy::Decompiler::Documentation {

// The C# `public interface IDocumentationProvider`. Abstract; a caller-supplied
// implementation owns the storage.
class IDocumentationProvider {
public:
    virtual ~IDocumentationProvider() = default;

    // The C# `string GetDocumentation(IEntity entity)` -- the XML documentation for
    // the given entity, or null (the port's empty `std::optional`) when none is
    // present.
    virtual std::optional<std::string> GetDocumentation(
        const TypeSystem::IEntity& entity) const = 0;
};

} // namespace ILSpy::Decompiler::Documentation
