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

// Port of ICSharpCode.BamlDecompiler/Baml/BamlDocument.cs (Ki, 2015, MIT).
// The BAML document: the flat record list the reader produces, plus the
// signature and the three format versions the header carries.
//
// C#-to-C++ porting decisions:
//  * The C# class derives from List<BamlRecord>: the port carries the same
//    list as a vector of owning pointers (Count/operator[]/Add model the
//    List surface the reader, the writer, and the node layer consume).
//    Owning unique_ptr keeps every deferred cross-reference (the defer
//    records' Record fields) a plain non-owning pointer into the same
//    document: records never move once added (the vector stores them
//    behind stable pointers), so a raw pointer stays valid for the
//    document's lifetime.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::BamlDecompiler::Baml {

class BamlRecord;

// The C# `BamlDocument.BamlVersion` nested struct: a ushort major/minor
// pair read straight from the header.
struct BamlVersion {
    std::uint16_t Major = 0;
    std::uint16_t Minor = 0;
};

// The C# `BamlDocument : List<BamlRecord>`.
class BamlDocument {
public:
    // The C# `DocumentName`: never assigned by the reader or writer (the
    // decompiler layers do not set it either); carried for the faithful
    // class shape.
    std::string DocumentName;

    std::string Signature;
    BamlVersion ReaderVersion;
    BamlVersion UpdaterVersion;
    BamlVersion WriterVersion;

    std::vector<std::unique_ptr<BamlRecord>> Records;

    std::size_t Count() const { return Records.size(); }

    BamlRecord& operator[](std::size_t index) { return *Records[index]; }
    const BamlRecord& operator[](std::size_t index) const { return *Records[index]; }

    void Add(std::unique_ptr<BamlRecord> record) {
        Records.push_back(std::move(record));
    }
};

} // namespace ILSpy::BamlDecompiler::Baml
