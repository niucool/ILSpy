// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/Documentation/XmlDocumentationProvider.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ILSpy::Decompiler::Documentation {

std::shared_ptr<XmlDocumentationProvider> XmlDocumentationProvider::LoadBeside(
    const std::string& assemblyFileName) {
    namespace fs = std::filesystem;
    if (assemblyFileName.empty())
        return nullptr;
    // The adjacent `<assembly>.xml` (the same base name, the .xml
    // extension): the compiler-generated documentation convention.
    fs::path xmlPath = fs::path(assemblyFileName).replace_extension(".xml");
    std::error_code ec;
    if (!fs::exists(xmlPath, ec) || ec)
        return nullptr;
    std::ifstream stream(xmlPath, std::ios::binary);
    if (!stream.is_open())
        return nullptr;
    std::ostringstream buffer;
    buffer << stream.rdbuf();

    auto provider = std::make_shared<XmlDocumentationProvider>();
    const std::string& content = buffer.str();
    // The `<member name="KEY">` elements: the name is the attribute's
    // quoted value; the content runs to the closing `</member>` (the
    // member elements never nest in the compiler-generated files).
    std::size_t position = 0;
    static constexpr const char* kMemberOpen = "<member name=\"";
    while (true) {
        std::size_t memberStart = content.find(kMemberOpen, position);
        if (memberStart == std::string::npos)
            break;
        std::size_t nameStart = memberStart + std::strlen(kMemberOpen);
        std::size_t nameEnd = content.find('"', nameStart);
        if (nameEnd == std::string::npos)
            break;
        std::size_t openTagEnd = content.find('>', nameEnd);
        if (openTagEnd == std::string::npos)
            break;
        std::string key = content.substr(nameStart, nameEnd - nameStart);
        // The self-closed `<member name="..."/>` carries no content (an
        // empty doc); the regular element runs to `</member>`.
        if (openTagEnd > 0 && content[openTagEnd - 1] == '/') {
            provider->members_[std::move(key)] = std::string();
            position = openTagEnd + 1;
            continue;
        }
        std::size_t contentStart = openTagEnd + 1;
        std::size_t contentEnd = content.find("</member>", contentStart);
        if (contentEnd == std::string::npos)
            break;
        provider->members_[std::move(key)] =
            content.substr(contentStart, contentEnd - contentStart);
        position = contentEnd + std::strlen("</member>");
    }
    return provider;
}

std::string XmlDocumentationProvider::GetDocumentation(
    const std::string& key) const {
    auto it = members_.find(key);
    return it != members_.end() ? it->second : std::string();
}

} // namespace ILSpy::Decompiler::Documentation
