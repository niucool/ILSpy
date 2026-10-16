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

#include "Decompiler/CSharp/Transforms/AddXmlDocumentationTransform.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/Documentation/IDocumentationProvider.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Xml/XmlConvert.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::Comment;
using Syntax::CommentType;
using Syntax::EntityDeclaration;

// The `CSharp` sub-namespace pulls `TypeSystem` into scope and shadows the
// type-system namespace (the `FixNameCollisions` precedent); the alias makes the
// entity interfaces explicit.
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;

namespace {

// The ASCII whitespace `char.IsWhiteSpace` covers for the doc strings in practice
// (the port's `Util::Char::IsWhiteSpace` works on UTF-16 units; the XML doc lines
// are ASCII-indented, so the byte-wise test is equivalent for every real doc snippet
// and avoids a UTF-8 <-> UTF-16 round trip per line).
bool IsAsciiWhiteSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// The C# `string.IsNullOrWhiteSpace`.
bool IsNullOrWhiteSpace(const std::string& value) {
    for (char c : value) {
        if (!IsAsciiWhiteSpace(c))
            return false;
    }
    return true;
}

// The C# `value.TrimStart().Length` complement: the count of leading whitespace
// characters, so `value.Substring(0, value.Length - value.TrimStart().Length)` is
// `value.substr(0, LeadingWhiteSpaceLength(value))`.
std::size_t LeadingWhiteSpaceLength(const std::string& value) {
    std::size_t i = 0;
    while (i < value.size() && IsAsciiWhiteSpace(value[i]))
        i++;
    return i;
}

// The `System.IO.StringReader.ReadLine` line splitter: a line ends at `\n`, `\r`,
// or `\r\n`; `false` is the C# `null` at end of input. The last line needs no
// trailing newline.
class StringLineReader {
public:
    explicit StringLineReader(const std::string& source) : source_(source) {}

    bool ReadLine(std::string& line) {
        if (position_ >= source_.size())
            return false;
        const std::size_t start = position_;
        std::size_t end = position_;
        while (end < source_.size() && source_[end] != '\r' && source_[end] != '\n')
            end++;
        line = source_.substr(start, end - start);
        if (end < source_.size()) {
            if (source_[end] == '\r' && end + 1 < source_.size() && source_[end + 1] == '\n')
                end += 2;
            else
                end += 1;
        }
        position_ = end;
        return true;
    }

private:
    const std::string& source_;
    std::size_t position_ = 0;
};

// The C# `node.GetResolveResult()` as the owning handle `AddAnnotation` needs: the
// first `ResolveResult` annotation's `shared_ptr`, or the shared
// `ErrorResolveResult.UnknownError()` singleton wrapped in a non-owning
// `shared_ptr` (the `ExpressionBuilder::ErrorExpression` precedent), so the comment
// carries exactly the object the C# `AddAnnotation(node.GetResolveResult())` adds.
std::shared_ptr<Sem::ResolveResult> GetResolveResultShared(const AstNode& node) {
    for (const auto& annotation : node.SharedAnnotations()) {
        if (dynamic_cast<Sem::ResolveResult*>(annotation.get()) != nullptr)
            return std::dynamic_pointer_cast<Sem::ResolveResult>(annotation);
    }
    return std::shared_ptr<Sem::ResolveResult>(
        const_cast<Sem::ErrorResolveResult*>(&Sem::ErrorResolveResult::UnknownError()),
        [](Sem::ResolveResult*) noexcept {});
}

} // namespace

void AddXmlDocumentationTransform::Run(AstNode& rootNode, TransformContext& context) {
    // The C# `if (!context.Settings.ShowXmlDocumentation ||
    // context.DecompileRun.DocumentationProvider == null) return;`.
    const ::ILSpy::Decompiler::Documentation::IDocumentationProvider* provider =
        context.DecompileRun().DocumentationProvider();
    if (!context.Settings().ShowXmlDocumentation() || provider == nullptr)
        return;
    try {
        // The C# `foreach (var entityDecl in
        // rootNode.DescendantsAndSelf.OfType<EntityDeclaration>())`.
        for (AstNode* node : rootNode.DescendantsAndSelf()) {
            auto* entityDecl = dynamic_cast<EntityDeclaration*>(node);
            if (entityDecl == nullptr)
                continue;
            // The C# `if (!(entityDecl.GetSymbol() is IEntity entity)) continue;`.
            const TS::ISymbol* symbol = GetSymbol(*entityDecl);
            const TS::IEntity* entity = dynamic_cast<const TS::IEntity*>(symbol);
            if (entity == nullptr)
                continue;
            std::optional<std::string> doc = provider->GetDocumentation(*entity);
            if (!doc.has_value()) {
                // A parameterized property is decompiled to its accessor methods;
                // show the property's documentation on the first accessor.
                const auto* accessor = dynamic_cast<const TS::IMethod*>(entity);
                const auto* owner = accessor != nullptr
                    ? dynamic_cast<const TS::IProperty*>(accessor->AccessorOwner())
                    : nullptr;
                if (owner != nullptr && TS::IsParameterizedProperty(*owner)) {
                    const TS::IMethod* first =
                        owner->Getter() != nullptr ? owner->Getter() : owner->Setter();
                    if (accessor == first)
                        doc = provider->GetDocumentation(*owner);
                }
            }
            if (doc.has_value()) {
                context.Step("Add XML documentation", entityDecl);
                InsertXmlDocumentation(*entityDecl, *doc);
            }
        }
    } catch (const ::ILSpy::Decompiler::Xml::XmlException& ex) {
        // The C# splits `(" Exception while reading XmlDoc: " + ex).Split('\r',
        // '\n', RemoveEmptyEntries)`; the port reports the exception's message (it
        // has no `Exception.ToString()`), minus the empty entries.
        const std::string message =
            std::string(" Exception while reading XmlDoc: ") + ex.what();
        StringLineReader reader(message);
        std::string line;
        while (reader.ReadLine(line)) {
            if (line.empty())
                continue;
            rootNode.AddLeadingTrivia(new Comment(line, CommentType::Documentation));
        }
    }
}

void AddXmlDocumentationTransform::InsertXmlDocumentation(AstNode& node,
                                                          const std::string& doc) {
    StringLineReader reader(doc);
    // Find the first non-empty line.
    std::string firstLine;
    do {
        if (!reader.ReadLine(firstLine))
            return;
    } while (IsNullOrWhiteSpace(firstLine));
    const std::string indentation =
        firstLine.substr(0, LeadingWhiteSpaceLength(firstLine));
    std::string line = firstLine;
    int skippedWhitespaceLines = 0;
    // Copy all lines from input to output, except for empty lines at the end.
    for (;;) {
        if (IsNullOrWhiteSpace(line)) {
            skippedWhitespaceLines++;
        } else {
            while (skippedWhitespaceLines > 0) {
                auto* emptyLine = new Comment(std::string(), CommentType::Documentation);
                emptyLine->AddAnnotation(GetResolveResultShared(node));
                node.AddLeadingTrivia(emptyLine);
                skippedWhitespaceLines--;
            }
            if (line.compare(0, indentation.size(), indentation) == 0)
                line = line.substr(indentation.size());
            auto* comment = new Comment(" " + line, CommentType::Documentation);
            comment->AddAnnotation(GetResolveResultShared(node));
            node.AddLeadingTrivia(comment);
        }
        if (!reader.ReadLine(line))
            break;
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
