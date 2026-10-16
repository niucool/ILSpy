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

#include "Decompiler/Util/Utf.hpp"

namespace ILSpy::Decompiler::Util {

namespace {
// Emit one Unicode code point as UTF-8.
void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
} // namespace

std::string Utf16ToUtf8(std::u16string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint16_t>(in[i]);
        // High surrogate: pair with a following low surrogate into a supplementary code point.
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < in.size()) {
            std::uint16_t lo = static_cast<std::uint16_t>(in[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                ++i;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
        }
        appendUtf8(out, cp);
    }
    return out;
}

std::u16string Utf8ToUtf16(std::string_view in) {
    std::u16string out;
    out.reserve(in.size());
    auto it = in.begin();
    while (it != in.end()) {
        std::uint32_t cp = static_cast<std::uint8_t>(*it++);
        unsigned int extra = 0;
        if ((cp & 0x80) == 0) {
            extra = 0;
        } else if ((cp & 0xE0) == 0xC0) {
            cp &= 0x1F; extra = 1;
        } else if ((cp & 0xF0) == 0xE0) {
            cp &= 0x0F; extra = 2;
        } else if ((cp & 0xF8) == 0xF0) {
            cp &= 0x07; extra = 3;
        } else {
            out.push_back(static_cast<char16_t>(0xFFFD));
            continue;
        }
        bool valid = true;
        for (unsigned int e = 0; e < extra; ++e) {
            if (it == in.end()) { valid = false; break; }
            std::uint8_t b = static_cast<std::uint8_t>(*it);
            if ((b & 0xC0) != 0x80) { valid = false; break; }
            ++it;
            cp = (cp << 6) | (b & 0x3F);
        }
        if (!valid) {
            out.push_back(static_cast<char16_t>(0xFFFD));
            continue;
        }
        if (cp > 0xFFFF) {
            // Encode as a surrogate pair.
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

} // namespace ILSpy::Decompiler::Util
