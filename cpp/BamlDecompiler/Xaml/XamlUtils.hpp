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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlUtils.cs (Ki, 2015, MIT) --
// the self-contained members plus the two `ToString(this XamlContext, ...)`
// extension methods (the former Phase-7-gated deferral, landed with the
// XamlContext/XamlType/XElement ports).
//
// Ported here:
//  * Escape(string) -- the XAML "{}" curly-brace escape a leading '{'
//    needs (a literal value that would otherwise parse as a markup
//    extension).
//  * ReadXamlDouble(this BinaryReader, bool scaledInt = false) -- the
//    WPF path/geometry binary double form: unscaled, a leading tag byte
//    selects 0/1/-1 constants, a scaled int32 (/ 1e6, dividing rather than
//    multiplying by the inverse to keep the original values -- the C#
//    comment), or a raw IEEE double; scaledInt=true reads the int32 form
//    directly. An unknown tag throws InvalidDataException ("Unknown double
//    type."), which the port maps to std::runtime_error carrying the exact
//    message (the port's InvalidDataException convention -- the XAML
//    decompiler lets it escape to the CLI global catch).
//  * EscapeName(StringBuilder, string)/(string) -- the characters XML
//    names cannot carry escape as \uXXXX: the char.IsWhiteSpace,
//    char.IsControl and char.IsSurrogate units over the UTF-16 code units
//    (so a supplementary-plane character renders as its TWO surrogate
//    halves, each escaped -- the C# `foreach (char)` walks the units).
//  * ToString(this XamlContext, XElement, XamlType) -- resolves the type's
//    XML namespace (mutating the type AND the element: the clr-namespace
//    fallback appends the `xmlns:<prefix>` declaration) and renders the
//    prefixed name through the XName overload below.
//  * ToString(this XamlContext, XElement, XName) -- the prefixed-name
//    render: no prefix when the name's namespace IS the element's default
//    namespace, else the in-scope prefix of the name's namespace (the
//    xml/xmlns reserved-prefix fallbacks included) when it is neither null
//    nor empty, then the local name.
//
// C#-to-C++ porting decisions:
//  * The port works in UTF-8 (std::string); EscapeName decodes to UTF-16
//    code units to reproduce the C# per-unit walk (Util::Utf8ToUtf16),
//    classifying each unit through Util::IsWhiteSpace/IsControl/IsSurrogate
//    and re-encoding the passing units. A lone surrogate half cannot be
//    carried by a UTF-8 string (the C# string could hold one and escape it;
//    the port's ill-formed-input replacement makes that shape unreachable
//    -- a documented divergence with no observable effect on well-formed
//    names).
//  * The `ReadXamlDouble` extension receiver ports to the plain
//    BamlBinaryReader& parameter (the port's System.IO.BinaryReader
//    stand-in over the .baml stream bytes).
//  * The two `ToString` extension receivers port to free functions taking
//    the context as the first parameter (C++ has no extension methods; the
//    ReadXamlDouble precedent). The XName overload's `ctx` parameter is
//    unused by the C# body too -- it ports unnamed.

#pragma once

#include <string>
#include <string_view>

#include "BamlDecompiler/Baml/BamlBinaryReader.hpp"

namespace ILSpy::Decompiler::Xml {
class XElement;
class XName;
}

namespace ILSpy::BamlDecompiler {
class XamlContext;
}

namespace ILSpy::BamlDecompiler::Xaml {

class XamlType;

// The C# `public static string Escape(string value)`.
std::string Escape(std::string_view value);

// The C# `public static double ReadXamlDouble(this BinaryReader reader,
// bool scaledInt = false)` -- see the file note for the wire form.
double ReadXamlDouble(Baml::BamlBinaryReader& reader, bool scaledInt = false);

// The C# `public static string ToString(this XamlContext ctx, XElement
// elem, XamlType type)` -- resolves the type's namespace against the element
// (see the file note for the mutation) and renders the prefixed name.
std::string ToString(XamlContext& ctx, ILSpy::Decompiler::Xml::XElement& elem,
    XamlType& type);

// The C# `public static string ToString(this XamlContext ctx, XElement
// elem, XName name)` (the receiver parameter is unused by the body and ports
// unnamed).
std::string ToString(const XamlContext& ctx,
    const ILSpy::Decompiler::Xml::XElement& elem,
    const ILSpy::Decompiler::Xml::XName& name);

// The C# `public static StringBuilder EscapeName(StringBuilder sb,
// string name)` -- appends the escaped name to the builder.
std::string& EscapeName(std::string& sb, std::string_view name);

// The C# `public static string EscapeName(string name)`.
std::string EscapeName(std::string_view name);

} // namespace ILSpy::BamlDecompiler::Xaml
