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

#include "Decompiler/Xml/XText.hpp"

#include <functional>

namespace ILSpy::Decompiler::Xml {

bool XText::DeepEquals(const XNode& other) const
{
    // The C# compares the NODE TYPES first, then blind-casts to XText (only
    // XText/XCData can carry the Text/CDATA node types). XCData therefore
    // never equals a plain XText even with the same text.
    if (NodeType() == other.NodeType())
        return text_ == static_cast<const XText&>(other).text_;
    return false;
}

std::int32_t XText::GetDeepHashCode() const
{
    return static_cast<std::int32_t>(std::hash<std::string> {}(text_));
}

} // namespace ILSpy::Decompiler::Xml
