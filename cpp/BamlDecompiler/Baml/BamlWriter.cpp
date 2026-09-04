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

// Port of the BamlWriter half of ICSharpCode.BamlDecompiler/Baml/BamlWriter.cs
// (Ki, 2015, MIT): the document serializer (see BamlWriter.hpp).

#include "BamlDecompiler/Baml/BamlBinaryWriter.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/Baml/BamlWriter.hpp"

#include "Decompiler/Util/Utf.hpp"

#include <vector>

namespace ILSpy::BamlDecompiler::Baml {

void WriteDocument(BamlDocument& doc, std::vector<std::uint8_t>& out) {
    out.clear();
    BamlBinaryWriter writer(out);
    {
        // The signature block (the C# writes it through a separate
        // Encoding.Unicode writer): the UTF-16 byte length (twice the char
        // count), the chars as UTF-16, then the zero padding to the 4-byte
        // multiple of the byte length.
        std::u16string units = ILSpy::Decompiler::Util::Utf8ToUtf16(doc.Signature);
        std::uint32_t length = static_cast<std::uint32_t>(units.size()) * 2;
        writer.WriteUInt32(length);
        writer.WriteChars(doc.Signature);
        writer.WriteBytes(
            std::vector<std::uint8_t>(((length + 3) & ~std::uint32_t{3}) - length, 0));
    }
    writer.WriteUInt16(doc.ReaderVersion.Major);
    writer.WriteUInt16(doc.ReaderVersion.Minor);
    writer.WriteUInt16(doc.UpdaterVersion.Major);
    writer.WriteUInt16(doc.UpdaterVersion.Minor);
    writer.WriteUInt16(doc.WriterVersion.Major);
    writer.WriteUInt16(doc.WriterVersion.Minor);

    std::vector<int> defers;
    for (std::size_t i = 0; i < doc.Count(); i++) {
        BamlRecord& rec = doc[i];
        rec.Position = writer.Position();
        writer.WriteByte(static_cast<std::uint8_t>(rec.Type()));
        rec.Write(writer);
        if (dynamic_cast<IBamlDeferRecord*>(&rec) != nullptr)
            defers.push_back(static_cast<int>(i));
    }
    for (int i : defers)
        dynamic_cast<IBamlDeferRecord&>(doc[i]).WriteDefer(doc, i, writer);
}

} // namespace ILSpy::BamlDecompiler::Baml
