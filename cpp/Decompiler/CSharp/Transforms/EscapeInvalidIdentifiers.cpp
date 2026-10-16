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

#include "Decompiler/CSharp/Transforms/EscapeInvalidIdentifiers.hpp"

#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <cstdio>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::Identifier;
namespace Util = ::ILSpy::Decompiler::Util;

bool EscapeInvalidIdentifiers::IsValid(char16_t ch) {
    return Util::IsLetterOrDigit(ch) || ch == u'_';
}

std::string EscapeInvalidIdentifiers::ReplaceInvalid(const std::string& s) {
    // The C# `s.Select(ch => ...)` walks the UTF-16 units of the .NET string; the
    // port's identifier names are UTF-8, so decode to UTF-16 first (the
    // AssignVariableNames.IsValidName precedent).
    const std::u16string units = Util::Utf8ToUtf16(s);
    std::u16string result;
    result.reserve(units.size());
    for (char16_t ch : units) {
        if (IsValid(ch)) {
            result.push_back(ch);
        } else {
            // The C# `string.Format("_{0:X4}", (int)ch)`: an uppercase-hex
            // 4-digit escape over the ASCII digits/letters (each is one BMP unit).
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "_%04X", static_cast<unsigned int>(ch));
            for (const char* p = buffer; *p != '\0'; ++p) {
                result.push_back(static_cast<char16_t>(static_cast<unsigned char>(*p)));
            }
        }
    }
    if (!result.empty() && !(Util::IsLetter(result[0]) || result[0] == u'_')) {
        result.insert(result.begin(), u'_');
    }
    return Util::Utf16ToUtf8(result);
}

void EscapeInvalidIdentifiers::Run(AstNode& rootNode, TransformContext& context) {
    for (AstNode* node : rootNode.DescendantsAndSelf()) {
        auto* ident = dynamic_cast<Identifier*>(node);
        if (ident == nullptr)
            continue;
        const std::string newName = ReplaceInvalid(ident->Name());
        if (newName != ident->Name()) {
            context.Step("Escape identifier '" + ident->Name() + "'", ident);
            ident->Name(newName);
        }
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
