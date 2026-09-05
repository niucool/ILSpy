// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
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

// The out-of-line XNamespace members (the well-known namespace singletons)
// and the out-of-line XName member (the expanded-name parse). The classes and
// the inline members live in the headers; see XNamespace.hpp / XName.hpp for
// the probed behaviors and the porting decisions.

#include "XName.hpp"

namespace ILSpy::Decompiler::Xml {

const XNamespace& XNamespace::None()
{
    static const XNamespace ns("");
    return ns;
}

const XNamespace& XNamespace::Xml()
{
    static const XNamespace ns("http://www.w3.org/XML/1998/namespace");
    return ns;
}

const XNamespace& XNamespace::Xmlns()
{
    static const XNamespace ns("http://www.w3.org/2000/xmlns/");
    return ns;
}

XName XName::Get(std::string expandedName)
{
    // ArgumentException.ThrowIfNullOrEmpty(expandedName, "expandedName"): the
    // empty-name arm (the null arm is unreachable).
    if (expandedName.empty())
        throw std::invalid_argument("The value cannot be an empty string. (Parameter 'expandedName')");
    if (expandedName[0] == '{') {
        // The C# splits at the LAST '}' (probed: "{a}b}c" parses to the
        // namespace "a}b" and the local name "c"), and the `num <= 1` guard
        // rejects both a missing '}' and an empty namespace ("{}a"); a '}' at
        // the very end is the empty local name.
        std::size_t num = expandedName.rfind('}');
        if (num == std::string::npos || num <= 1 || num == expandedName.size() - 1)
            throw std::invalid_argument("'" + expandedName + "' is an invalid expanded name.");
        std::string ns = expandedName.substr(1, num - 1);
        std::string local = expandedName.substr(num + 1);
        return XNamespace::Get(std::move(ns)).GetName(std::move(local));
    }
    return XNamespace::None().GetName(std::move(expandedName));
}

} // namespace ILSpy::Decompiler::Xml
