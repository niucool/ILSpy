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

// Port-authored stand-in for the System.Xml helpers the BAML decompiler
// consumes (the C# ICSharpCode.BamlDecompiler uses the BCL classes directly;
// there is no ICSharpCode source file to mirror, so this fills the same gap
// the vendored winmd reader fills for System.Reflection.Metadata):
//
//  * XmlConvert.EncodeLocalName -- the NCName escaper every XamlType /
//    XamlProperty ToXName call site routes its names through (the BAML
//    type/member names are raw identifiers that must become valid XML
//    NCNames, escaped as "_xXXXX_" runs).
//  * XmlConvert.VerifyNCName -- the NCName validation XName.Get performs on
//    every local name.
//  * XmlException -- the System.Xml exception type both throw, with the exact
//    .NET message shapes.
//
// Every behavior was gold-pinned against the real .NET 10 classes through the
// C:/temp-probe/XmlNameProbe probe (a full 65536-unit classification sweep for
// EncodeLocalName AND a second full sweep through XName.Get for the
// VerifyNCName tables -- which turned out extensionally identical), plus the
// decompiled .NET 10 System.Private.Xml XmlConvert.EncodeName algorithm and
// the System.Xml.Linq XName/XNamespace pair (see XName.hpp / XNamespace.hpp):
//
//  * EncodeLocalName("") returns "" unchanged (string.IsNullOrEmpty early
//    out); there is no "_" fallback for the empty name.
//  * Escapes are "_x" + uppercase hex + "_": X4 for BMP units, X8 for
//    surrogate-pair code points (EncodeLocalName("emoji-face") is
//    "_x0001F600_"); a LONE surrogate half escapes as X4 ("_xD800_").
//  * The escape hex digits are UPPERCASE; the XML char tables are the
//    XML 1.0 4th-edition set (U+017F long s, U+00B5 micro, U+FDFD, U+F900
//    and everything past U+9FA5 are escaped; U+3007 is a start char).
//  * An underscore that would start an "_xXXXX_" (or "_xXXXXXXXX_")
//    escape sequence is itself escaped as "_x005F_" so decoding is
//    unambiguous: "_x0041_" -> "_x005F_x0041_" (the recognizer is the
//    .NET 10 generated regex `(?<=_)[Xx][0-9a-fA-F]{4}(?:_|[0-9a-fA-F]{4}_)`
//    and it accepts both hex letter cases and both the 4- and 8-digit
//    escape tails).
//  * VerifyNCName message shapes (XmlException):
//      - first unit invalid: "Name cannot begin with the '{0}' character,
//        hexadecimal value 0x{1:X2}."
//      - later unit invalid: "The '{0}' character, hexadecimal value
//        0x{1:X2}, cannot be included in a name."
//    where {0} is the invalid unit -- the NUL unit renders as '.' (the
//    XmlException.BuildCharExceptionArgs quirk), and a high surrogate
//    followed by any non-NUL unit renders BOTH units with the
//    CombineSurrogateChar value (which recombines a real pair to its code
//    point: "\uD83D\uDE00" reports 0x1F600, but a lone "\uD800" followed by
//    'b' reports the raw (low - 56320) | ((high - 55296) << 10 + 65536)
//    garbage value 0xFFFF2462 -- a .NET quirk the port reproduces).
//  * The C# string arguments are UTF-16; the port takes UTF-8 (decision D2)
//    and decodes it permissively: valid sequences decode to code points in
//    [0, 0x10FFFF] including the surrogate range (a lone surrogate can reach
//    the port only as WTF-8 bytes; it escapes per-unit like .NET), and
//    malformed input decodes to U+FFFD.
//
// C#-to-C++ porting decisions:
//  * XmlException maps to a std::runtime_error subclass so tests can assert
//    the exact type and message; ArgumentException maps to
//    std::invalid_argument carrying the exact .NET text. The C# null checks
//    (ArgumentNullException) are unreachable -- std::string has no null.
//  * Only the members the BAML decompiler reaches are ported: EncodeLocalName
//    and VerifyNCName. EncodeName/EncodeNmToken/DecodeName and the rest of
//    the XmlConvert surface stay unported (the System.Xml.Linq DOM slice
//    this feeds will extend this file when a consumer appears).

#pragma once

#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::Xml {

// The System.Xml.XmlException the NCName validation throws (a distinct type
// so consumers can catch it separately from the ArgumentException-mapped
// std::invalid_argument).
class XmlException : public std::runtime_error {
public:
    explicit XmlException(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

// XmlConvert.EncodeLocalName: returns the name with every unit that is not
// valid in an XML NCName escaped as "_xXXXX_" (and "_" itself escaped when it
// would introduce an escape sequence). The input is UTF-8; the result is UTF-8.
std::string EncodeLocalName(const std::string& name);

// XmlConvert.VerifyNCName: throws XmlException when the name is not a valid
// NCName (no colons, first unit a start char), std::invalid_argument when it
// is empty; returns the name unchanged otherwise.
std::string VerifyNCName(const std::string& name);

} // namespace ILSpy::Decompiler::Xml
