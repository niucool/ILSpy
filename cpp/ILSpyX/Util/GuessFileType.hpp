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

// Port of ICSharpCode.ILSpyX/Util/GuessFileType.cs: the file-type sniffer
// (Binary / Text / Xml) the resource-view surfaces use to decide whether a
// file's bytes render as text. The three-stage C# pipeline ports directly:
// the two-byte BOM dispatch, the RFC 3629 UTF-8 state machine over at most
// 500 KB, and the XmlTextReader MoveToContent probe over the decoded
// text.
//
// C#-to-C++ porting decisions:
//  * The C# `public static class GuessFileType` ports as free functions
//    (the SRMExtensions free-function convention for static C# classes).
//  * The C# `Stream` parameter ports as the (data, size) byte view (the
//    repo's blob convention): the C# only ever seeks, reads forward, and
//    rewinds, which the view models exactly.
//  * The C# `StreamReader` decodes (BOM-detected UTF-16/UTF-32 or UTF-8,
//    invalid sequences to U+FFFD) ports to a file-local decode step; the
//    XML probe runs over the port's gold-pinned Xml parser
//    (ParseDocumentText, the XmlTextParser port) -- the C#
//    XmlTextReader.MoveToContent accepts any document whose FIRST content
//    node parses, so content after a complete root element never fails
//    the C# probe (a second root element or trailing root-level junk
//    still reports Xml); the whole-document parse the port performs is
//    stricter there, a documented divergence: trailing root-level
//    content reports Text where the C# reports Xml (no real consumer
//    produces such files; every well-formed and every root-malformed
//    input classifies identically).
//  * The C# `XmlException` catch maps to the parser's XmlException
//    (std::runtime_error subclass): anything the parser rejects makes the
//    file Text.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ILSpy::ILSpyX::Util {

// The C# `public enum FileType`.
enum class FileType {
    Binary,
    Text,
    Xml,
};

// The C# `public static FileType DetectFileType(Stream stream)`: Binary
// for anything under two bytes, a non-BOM/non-UTF-8 byte stream, or a
// truncated UTF-8 BOM; Xml when the decoded text starts a well-formed XML
// document; Text for valid UTF-8 text that does not.
FileType DetectFileType(const std::uint8_t* data, std::size_t size);

}  // namespace ILSpy::ILSpyX::Util
