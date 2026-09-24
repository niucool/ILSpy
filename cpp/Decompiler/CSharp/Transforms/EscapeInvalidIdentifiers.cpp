// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
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

#include "Decompiler/CSharp/Transforms/EscapeInvalidIdentifiers.hpp"

#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"

#include <cctype>
#include <cstdio>
#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {
namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;

// The C# `bool IsValid(char ch)` -- `char.IsLetterOrDigit(ch)` is
// Unicode-aware; the port's ASCII-only code/comments convention keeps the
// identifier alphabet to the C# `char` ranges via the locale-independent
// `std::iswalnum` shape over unsigned chars plus the underscore.
bool IsValid(char ch) {
    unsigned char u = static_cast<unsigned char>(ch);
    if (u == '_') return true;
    if (u < 0x80) return std::isalnum(u) != 0;
    return false;
}

// The C# `string ReplaceInvalid(string s)` -- every invalid character becomes
// `_XXXX` (the 4-hex-digit code); a leading non-letter is underscored.
std::string ReplaceInvalid(const std::string& s) {
    std::string name;
    name.reserve(s.size());
    char hex[8];
    for (unsigned char ch : s) {
        if (IsValid(static_cast<char>(ch))) {
            name.push_back(static_cast<char>(ch));
        } else {
            std::snprintf(hex, sizeof(hex), "_%04X", static_cast<unsigned int>(ch));
            name += hex;
        }
    }
    if (!name.empty() && !(std::isalpha(static_cast<unsigned char>(name[0])) != 0 ||
                           name[0] == '_')) {
        name = "_" + name;
    }
    return name;
}

} // namespace

void EscapeInvalidIdentifiers::Run(::ILSpy::Decompiler::CSharp::Syntax::AstNode& rootNode,
                                   TransformContext& context) {
    for (Syntax::AstNode* node : rootNode.DescendantsAndSelf()) {
        auto* ident = dynamic_cast<Syntax::Identifier*>(node);
        if (ident == nullptr) continue;
        const std::string newName = ReplaceInvalid(ident->Name());
        if (newName != ident->Name()) {
            context.StepOnce("Escape identifier '" + ident->Name() + "'", ident);
            ident->Name(newName);
        }
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms