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

// Implementation of ILSpyX/Util/GuessFileType.hpp (the pipeline decisions
// are documented on the header).

#include "ILSpyX/Util/GuessFileType.hpp"

#include "Decompiler/Xml/XmlTextParser.hpp"

#include <string>

namespace ILSpy::ILSpyX::Util {
namespace {

// The C# `StreamReader`'s BOM-aware decode: UTF-16LE (FF FE, or FF FE
// 00 00 as the UTF-32LE BOM), UTF-16BE (FE FF), and UTF-8 (EF BB BF or
// raw), each consumed and decoded to the port's UTF-8 string. Invalid
// code units become U+FFFD exactly as the .NET decoders' replacement
// fallback does (they never throw).
void AppendUtf8(std::string& out, std::uint32_t code)
{
    if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        // A lone surrogate or out-of-range scalar: the replacement char
        // (the .NET DecoderExceptionFallback is not in play; the defaults
        // replace).
        code = 0xFFFD;
    }
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// Decode a UTF-16LE/BE image (the BOM already skipped) to UTF-8.
std::string DecodeUtf16(const std::uint8_t* data, std::size_t size, bool bigEndian)
{
    std::string text;
    for (std::size_t i = 0; i + 1 < size; i += 2) {
        std::uint16_t unit = bigEndian
            ? static_cast<std::uint16_t>((data[i] << 8) | data[i + 1])
            : static_cast<std::uint16_t>((data[i + 1] << 8) | data[i]);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            // A lead surrogate: pair with a trail surrogate from the next
            // unit, or a replacement char.
            if (i + 3 < size) {
                std::uint16_t next = bigEndian
                    ? static_cast<std::uint16_t>((data[i + 2] << 8) | data[i + 3])
                    : static_cast<std::uint16_t>((data[i + 3] << 8) | data[i + 2]);
                if (next >= 0xDC00 && next <= 0xDFFF) {
                    AppendUtf8(text, 0x10000
                        + ((static_cast<std::uint32_t>(unit) - 0xD800) << 10)
                        + (next - 0xDC00));
                    i += 2;
                    continue;
                }
            }
            AppendUtf8(text, 0xFFFD);
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            AppendUtf8(text, 0xFFFD);  // a lone trail surrogate
        } else {
            AppendUtf8(text, unit);
        }
    }
    return text;
}

// Decode a UTF-32LE image (the BOM already skipped) to UTF-8.
std::string DecodeUtf32LE(const std::uint8_t* data, std::size_t size)
{
    std::string text;
    for (std::size_t i = 0; i + 3 < size; i += 4) {
        AppendUtf8(text,
            static_cast<std::uint32_t>(data[i])
                | (static_cast<std::uint32_t>(data[i + 1]) << 8)
                | (static_cast<std::uint32_t>(data[i + 2]) << 16)
                | (static_cast<std::uint32_t>(data[i + 3]) << 24));
    }
    return text;
}

// Decode a UTF-8 image to the port's UTF-8 string, replacing invalid
// sequences with U+FFFD (the .NET UTF8Encoding replacement fallback --
// the BOM arm's StreamReader never throws on bad bytes).
std::string DecodeUtf8(const std::uint8_t* data, std::size_t size)
{
    std::string text;
    std::size_t i = 0;
    while (i < size) {
        std::uint8_t b = data[i];
        if (b < 0x80) {
            text.push_back(static_cast<char>(b));
            i++;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t code = 0;
        if (b >= 0xC2 && b < 0xE0) {
            length = 2;
            code = b & 0x1F;
        } else if (b >= 0xE0 && b < 0xF0) {
            length = 3;
            code = b & 0x0F;
        } else if (b >= 0xF0 && b < 0xF5) {
            length = 4;
            code = b & 0x07;
        } else {
            AppendUtf8(text, 0xFFFD);  // an invalid lead byte
            i++;
            continue;
        }
        if (i + length > size) {
            AppendUtf8(text, 0xFFFD);  // a truncated sequence
            i++;
            continue;
        }
        bool valid = true;
        for (std::size_t j = 1; j < length; j++) {
            std::uint8_t cont = data[i + j];
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (cont & 0x3F);
        }
        if (!valid) {
            AppendUtf8(text, 0xFFFD);
            i++;
            continue;
        }
        AppendUtf8(text, code);
        i += length;
    }
    return text;
}

// The C# `static bool IsUTF8(Stream fs, byte firstByte, byte secondByte)`:
// the RFC 3629 state machine over at most 500 KB (the first two bytes
// re-fed by the caller).
bool IsUtf8(const std::uint8_t* data, std::size_t size,
    std::uint8_t firstByte, std::uint8_t secondByte)
{
    const std::size_t max = size < 500000 ? size : 500000;
    enum State { ASCII, Error, UTF8, UTF8Sequence };
    int state = ASCII;
    int sequenceLength = 0;
    std::uint8_t b;
    for (std::size_t i = 0; i < max; i++) {
        if (i == 0) {
            b = firstByte;
        } else if (i == 1) {
            b = secondByte;
        } else {
            b = data[i];
        }
        if (b < 0x80) {
            // normal ASCII character
            if (state == UTF8Sequence) {
                state = Error;
                break;
            }
        } else if (b < 0xC0) {
            // 10xxxxxx : continues UTF8 byte sequence
            if (state == UTF8Sequence) {
                --sequenceLength;
                if (sequenceLength < 0) {
                    state = Error;
                    break;
                } else if (sequenceLength == 0) {
                    state = UTF8;
                }
            } else {
                state = Error;
                break;
            }
        } else if (b >= 0xC2 && b < 0xF5) {
            // beginning of byte sequence
            if (state == UTF8 || state == ASCII) {
                state = UTF8Sequence;
                if (b < 0xE0) {
                    sequenceLength = 1;  // one more byte following
                } else if (b < 0xF0) {
                    sequenceLength = 2;  // two more bytes following
                } else {
                    sequenceLength = 3;  // three more bytes following
                }
            } else {
                state = Error;
                break;
            }
        } else {
            // 0xc0, 0xc1, 0xf5 to 0xff are invalid in UTF-8 (see RFC 3629)
            state = Error;
            break;
        }
    }
    return state != Error;
}

// The C# `XmlTextReader.MoveToContent()` probe: the decoded text parses
// as a well-formed XML document (see the header for the
// trailing-content divergence note).
bool ParsesAsXmlDocument(const std::string& text)
{
    try {
        ILSpy::Decompiler::Xml::ParseDocumentText(text);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

FileType DetectFileType(const std::uint8_t* data, std::size_t size)
{
    if (size < 2)
        return FileType::Binary;

    const std::uint8_t firstByte = data[0];
    const std::uint8_t secondByte = data[1];
    const std::uint16_t magic =
        static_cast<std::uint16_t>((firstByte << 8) | secondByte);
    std::string text;
    switch (magic) {
        case 0xFFFE:  // UTF-16 LE BOM / UTF-32 LE BOM
        case 0xFEFF: {  // UTF-16 BE BOM
            // The StreamReader's BOM detection: FF FE 00 00 is the
            // UTF-32LE BOM; FF FE followed by anything else is UTF-16LE;
            // FE FF is UTF-16BE. Each arm decodes with replacement.
            if (magic == 0xFFFE) {
                if (size >= 4 && data[2] == 0 && data[3] == 0)
                    text = DecodeUtf32LE(data + 4, size - 4);
                else
                    text = DecodeUtf16(data + 2, size - 2, false);
            } else {
                text = DecodeUtf16(data + 2, size - 2, true);
            }
            break;
        }
        case 0xEFBB: {  // start of UTF-8 BOM
            if (size >= 3 && data[2] == 0xBF) {
                // The UTF-8 StreamReader arm: the BOM is consumed and the
                // payload decodes with replacement (never Binary here).
                text = DecodeUtf8(data + 3, size - 3);
                break;
            }
            return FileType::Binary;
        }
        default: {
            if (IsUtf8(data, size, firstByte, secondByte)) {
                text = DecodeUtf8(data, size);
                break;
            }
            return FileType::Binary;
        }
    }

    return ParsesAsXmlDocument(text) ? FileType::Xml : FileType::Text;
}

}  // namespace ILSpy::ILSpyX::Util
